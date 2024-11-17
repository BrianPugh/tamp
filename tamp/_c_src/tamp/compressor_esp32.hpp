#pragma once

#include "compressor.h"
// #include <stdlib.h>
// #include <stdbool.h>
#include <cstring>

#include "private/tamp_search.hpp"
#include "private/copyutil.hpp"

namespace tamp {

    inline constexpr TampConf CONF_DEFAULT {.window=10, .literal=8, .use_custom_dictionary=false};

    namespace detail {

        template<typename T>
        requires (!std::is_const_v<T>)
        class OutArg {
            T* ptr;
            public:
            constexpr OutArg(T* ptr) noexcept :
                ptr {ptr}
            {

            }

            constexpr OutArg& operator =(const T& val) noexcept {
                if(this->ptr) {
                    *ptr = val;
                }
                return *this;
            }

            constexpr const OutArg& operator =(const T& val) const noexcept {
                if(this->ptr) {
                    *ptr = val;
                }
                return *this;
            }    

        };


        /* Type traits to encapsulate handling of existing and non-existing member TampCompressor::conf_literal */
        template<typename C>
        struct LitConf {
            static constexpr bool HAS_VAR_LITLEN = false;
            static constexpr uint32_t LITLEN = 8;
            static constexpr uint32_t LITFLAG = 1 << LITLEN;
            static constexpr uint32_t getLiteralBits(const C& comp) noexcept {
                return LITLEN;
            }
            static constexpr uint32_t getLiteralFlag(const C& comp) noexcept {
                return 1 << LITLEN;
            }    
            static constexpr bool isValidLitLen(const uint8_t bits) noexcept {
                return bits == LITLEN;
            }
            static constexpr void setLiteralBits(const C& comp, const uint8_t bits) noexcept {
                // Nothing.
            }    
            static constexpr bool isValidLiteral(const C& comp, const uint32_t c) noexcept {
                return true;
            }
        };

        template<typename C>
        requires requires (const C& comp) {{comp.conf_literal};}
        struct LitConf<C> {
            static constexpr bool HAS_VAR_LITLEN = true;
            static constexpr uint32_t getLiteralBits(const C& comp) noexcept {
                return comp.conf_literal;
            }
            static constexpr uint32_t getLiteralFlag(const C& comp) noexcept {
                return 1 << getLiteralBits(comp);
            }       
            static constexpr void setLiteralBits(const C& comp, const uint8_t bits) noexcept {
                comp.conf_literal = bits;
            }
            static constexpr bool isValidLitLen(const uint8_t bits) noexcept {
                return bits >= 5 && bits <= 8;
            }
            static constexpr bool isValidLiteral(const C& comp, const uint32_t c) noexcept {
                return c < getLiteralFlag(comp);
            }    
        };

        struct huffcode_t {
            /**
             * @brief Huffman code value
             * 
             */
            uint8_t code;
            /**
             * @brief Size of the code value in bits.
             * 
             */
            uint8_t n_bits;
            constexpr huffcode_t(const uint8_t code, const uint8_t n_bits) noexcept :
                code {code},
                n_bits {n_bits}
            {

            }

        };
    } // namespace detail



    class Compressor : public TampCompressor {
        using LitLenTrait = detail::LitConf<TampCompressor>;        
        public:

            static constexpr uint32_t INBUF_SIZE = sizeof(TampCompressor::input);

            /**
             * @brief Does this compressor support configurations with \c TampConf::literal != 8? 
             * 
             */
            static constexpr bool HAS_VAR_LITLEN = LitLenTrait::HAS_VAR_LITLEN;


            static Compressor& of(TampCompressor* comp) noexcept {
                return static_cast<Compressor&>(*comp);
            }

            static Compressor& of(TampCompressor& comp) noexcept {
                return static_cast<Compressor&>(comp);
            }

            constexpr Compressor() noexcept = default;

            tamp_res init(const TampConf* conf, unsigned char* const window) noexcept {
                if(!conf) {
                    conf = &CONF_DEFAULT;
                } else {
                    if( conf->window < 8 || conf->window > 15)
                        return TAMP_INVALID_CONF;
                    if(window == nullptr) {
                        return TAMP_ERROR;
                    }                    
                }


                // tamp::Locator::stats.reset();

                // std::memset(compressor,0,sizeof(*compressor));
                this->reset();

                if(!this->setLitLen(conf->literal)) [[unlikely]] {
                    return TAMP_INVALID_CONF;
                }

                this->setWindowSizeLog2(conf->window);

                this->window = window;
                this->min_pattern_size = tamp_compute_min_pattern_size(conf->window, this->getLitLen());

                if(!(conf->use_custom_dictionary))
                    tamp_initialize_dictionary(window, this->windowSize());

                // Write header to bit buffer
                write_to_bit_buffer(this->getWindowSizeLog2() - 8, 3);
                write_to_bit_buffer(this->getLitLen() - 5, 2);
                write_to_bit_buffer(conf->use_custom_dictionary, 1);
                write_to_bit_buffer(0,1); // Reserved
                write_to_bit_buffer(0,1); // No more header bytes

                return TAMP_OK;
            }

            size_t sink(
                    const unsigned char* const input,
                    size_t input_size
                    ) {

                {
                    const uint32_t space = INBUF_SIZE - this->input_size; 
                    input_size = min(input_size,space);
                }

                uint8_t* const ip = this->input + this->input_size;
                this->input_size += input_size;

                this->bufcpy(ip,input,input_size);      

                return input_size;
            }

            tamp_res compress_poll(unsigned char *output, size_t output_size, size_t* const output_written_size) {

                const detail::OutArg output_written_sz {output_written_size};

                // Flush any whole bytes from the bit buffer:
                if(this->bufferedByteCnt() != 0) {
                    // Make sure there's enough room in the bit buffer.
                    size_t flush_bytes_written;
                    const tamp_res res = this->flush_bytes(output, output_size, flush_bytes_written);
                    output_written_sz = flush_bytes_written;
                    if(TAMP_UNLIKELY(res != TAMP_OK))
                        return res;
                    output_size -= flush_bytes_written;
                } else {
                    output_written_sz = 0;
                }

                if(TAMP_UNLIKELY(this->input_size == 0)) {
                    // output_written_sz = 0;
                    return TAMP_OK;
                }

                // // TODO Actually, we'd want to partially flush even if input_size == 0.
                // {
                //     // Make sure there's enough room in the bit buffer.
                //     size_t flush_bytes_written;
                //     // tamp_res res = flush_bytes(compressor, output, output_size, &flush_bytes_written);
                //     const tamp_res res = flush_bytes(comp, output, output_size, flush_bytes_written);
                //     // set(output_written_size, flush_bytes_written);
                //     output_written_sz = flush_bytes_written;
                //     if(TAMP_UNLIKELY(res != TAMP_OK))
                //         return res;
                //     output_size -= flush_bytes_written;
                //     // output += flush_bytes_written;
                // }

                if(TAMP_UNLIKELY(output_size == 0))
                    return TAMP_OUTPUT_FULL;

                tamp::byte_span match {};

                if(TAMP_LIKELY(this->input_size >= this->min_pattern_size))
                {
                    const uint32_t patLen = min(this->input_size, this->maxPatternLen());

                    match = tamp::Locator::find_longest_match(
                        this->input,
                        patLen,
                        this->window,
                        this->windowSize()
                    );
                }

                if(match.size() >= this->min_pattern_size) {
                    const uint32_t match_size = match.size();
                    const uint32_t match_index = match.data() - this->window;
                    this->writeBackref(match_index, match_size);
                    // Move matched data from input to window.
                    this->moveInputToWindow(match_size);
                } else {
                    // We couldn't find a match/back-reference. Output a single literal and return.
                    // Write LITERAL
                    
                    // This makes gcc 12.2.0 introduce some unnecessary jumps, costing about 1% in overall performance.
                    // return comp.handleLiteral();
                    // So manual inlining it is:

                    {
                        const uint32_t c = this->input[ 0 ];
                        if(this->isValidLiteral(c)) [[likely]] {
                            const uint32_t LF = this->getLiteralFlag();
                            this->write_to_bit_buffer(LF | c, this->getLitLen() + 1);

                            // Move 1 byte from input to window:
                            const uint32_t wp = this->window_pos;
                            this->window[wp] = c;
                            this->window_pos = (wp + 1) & (this->windowSize() - 1);
                            this->consumeInput(1);

                        } else {
                            return TAMP_EXCESS_BITS;
                        }
                    }
                }

                return TAMP_OK;
            }        

            tamp_res flush(
                    unsigned char *output,
                    size_t output_size,
                    size_t *output_written_size,
                    bool write_token
                    ) noexcept {
                tamp_res res;
                size_t chunk_output_written_size;

                const detail::OutArg output_written_sz {output_written_size};

                unsigned char* const out_start = output;

                while(this->input_size){
                    // Compress the remainder of the input buffer.
                    res = this->compress_poll(output, output_size, &chunk_output_written_size);
                    output_size -= chunk_output_written_size;
                    output += chunk_output_written_size;
                    if(TAMP_UNLIKELY(res != TAMP_OK)) {
                        output_written_sz = output-out_start;
                        return res;
                    }
                }

                // Perform partial flush to see if we need a FLUSH token, and to subsequently
                // make room for the FLUSH token.
                res = this->flush_bytes(output, output_size, chunk_output_written_size);
                output_size -= chunk_output_written_size;
                if(TAMP_UNLIKELY(res != TAMP_OK)) {
                    output_written_sz = output-out_start;
                    return res;
                }

                // assert comp.bit_buffer_pos <= 7
                // Check if there's enough output buffer space
                if (this->bit_buffer_pos != 0){
                    // Need to output at least 1 more byte.
                    if (output_size == 0){
                        output_written_sz = output-out_start;
                        return TAMP_OUTPUT_FULL;
                    }
                    if(write_token){
                        // Need to output 1 extra byte for the FLUSH token.
                        if(output_size < 2) {
                            output_written_sz = output-out_start;
                            return TAMP_OUTPUT_FULL;
                        }
                        write_to_bit_buffer(Compressor::FLUSH_CODE, 9);
                    }

                    {
                        // assert 1 <= bit_buffer_pos && bit_buffer_pos <= 16 (=7+9)
                        *output++ = this->bit_buffer >> 24;
                        if(this->bit_buffer_pos > 8) {
                            // assert(write_token);
                            *output++ = this->bit_buffer >> 16;
                        }
                        this->bit_buffer = 0;
                        this->bit_buffer_pos = 0;
                    }        
                }

                output_written_sz = output-out_start;
                return TAMP_OK;
            }



            private:

            static constexpr uint8_t FLUSH_CODE = 0xAB;

            static constexpr uint32_t INBUF_ALIGNMENT = alignof(TampCompressor::input);

            static constexpr uint32_t min(const uint32_t a, const uint32_t b) {
                return a < b ? a : b;
            }

            void reset() noexcept {
                // Reset the state of this instance, but not its configuration.
                // We're _not_ clearing the window's contents here.
                this->bit_buffer = 0;
                this->window_pos = 0;
                // this->window_size = 0;
                this->bit_buffer_pos = 0;
                this->input_size = 0;
                // this->conf_window = 0;
                // this->min_pattern_size = 0;
            }

            /**
             * @brief Copy \p len (<= INBUF_SIZE) bytes from \p src to \p dst, assuming
             * that short copies (len <= 4) are more likely/frequent than longer ones.
             * 
             * @param dst 
             * @param src 
             * @param len 
             */
            static void bufcpy(uint8_t* dst, const uint8_t* src, const uint32_t len) noexcept {
                switch(len) {
                    case 1: 
                        mem::cpy_short<1>(dst,src); 
                        break;
                    // This would rarely, if ever, happen:
                    // case 2:
                    //     mem::cpy_short<2>(dst,src);
                    //     break;
                    case 3:
                        mem::cpy_short<3>(dst,src); 
                        break;                    
                    case 4:
                        mem::cpy_short<4>(dst,src); 
                        break;
                    default:
                        if(len < INBUF_SIZE) [[likely]] {
                            mem::cpy_short<INBUF_SIZE>(dst, src, len);                
                        } else {
                            // assert(input_size == INBUF_SIZE);
                            // assert(this->input_size == 0);
                            mem::cpy_short<INBUF_SIZE>(dst, src);
                        }
                }
            }


            /**
             * @brief Remove the first \p len bytes from the input buffer. After this,
             * the next byte of input is at \c input[0] again.
             *  
             * @param len must be > 0 and <= input_size
             */
            void consumeInput(const uint32_t len) noexcept {
                /* Overall it's faster to just copy all remaining bytes from (input+len)
                to (input) once per sink+poll cycle than dealing with a circular buffer 
                three times (sink, pattern search, poll) each cycle; especially since 
                we need input to be consecutive in memory for the pattern search anyway.
                */
                // assert(len > 0);
                // assert(len <= this->input_size);
                const uint32_t sz = this->input_size - len;
                this->input_size = sz;

                if (sz != 0) [[likely]] {
                    if constexpr (tamp::Arch::ESP32S3 && INBUF_SIZE == 16 && INBUF_ALIGNMENT >= 16) {
                        /* When this->input is 16-byte aligned, we can just load,
                        align, and store 128 bits and be done.
                        */

                        // 4 clock cycles:
                        asm (
                            "LD.QR q0, %[input], 0" "\n"
                            "WUR.SAR_BYTE %[len]" "\n" // Pipelining: This instruction comes for free.
                            "EE.SRC.Q q0, q0, q0" "\n"
                            "ST.QR q0, %[input], 0" "\n"
                            : "+m" (this->input)
                            : [input] "r" (&this->input),
                            [len] "r" (len)
                        );            
                    } else {        
                        // Just copying 16 bytes is faster than copying the (variable) exact length (< 16).
                        // 9 clock cycles:
                        mem::cpy_short<INBUF_SIZE>(this->input, this->input + len);
                    }
                }
            }

            /**
             * @brief Copy \p len bytes from \c input to \c window, update the current window position
             * and remove the bytes from \c input.
             * 
             * @param len must be > 0 and <= input_size
             */
            void moveInputToWindow(const uint32_t len) noexcept {
                addToWindow(this->input,len);
                consumeInput(len);
            }

            constexpr void write_to_bit_buffer(const uint32_t bits, const uint32_t n_bits) noexcept {
                uint32_t pos = this->bit_buffer_pos;
                pos += n_bits;
                this->bit_buffer_pos = pos;
                this->bit_buffer |= bits << (32 - pos);
            } 

            constexpr void write_to_bit_buffer(const detail::huffcode_t& huffcode) noexcept {
                write_to_bit_buffer(huffcode.code, huffcode.n_bits);
            }       

            constexpr void writeBackref(const uint32_t match_index, const uint32_t match_size) noexcept {
                // Write TOKEN
                const uint32_t huffman_index = match_size - this->min_pattern_size;
                write_to_bit_buffer(huffman_codes[huffman_index]);
                write_to_bit_buffer(match_index, this->conf_window);
            }

            constexpr void writeLiteral(const uint32_t lit) noexcept {
                write_to_bit_buffer(getLiteralFlag() | lit, getLitLen() + 1);
            }

            tamp_res handleLiteral() noexcept {
                const uint32_t lit = this->input[0];
                if(isValidLiteral(lit)) [[likely]] {   
                    writeLiteral(lit);
                    {
                        // Add one byte to window:
                        const uint32_t wp = this->window_pos;
                        this->window[wp] = lit;
                        this->window_pos = (wp + 1) & (this->window_size - 1);
                    }
                    this->consumeInput(1);
                    return TAMP_OK;
                } else {
                    return TAMP_EXCESS_BITS;
                }
            }

            constexpr uint32_t bufferedByteCnt() const noexcept {
                return this->bit_buffer_pos / 8;
            }

            /**
             * @brief Partially flush the internal bit buffer.
             *
             * Up to 7 bits may remain in the internal bit buffer.
             */
            tamp_res flush_bytes(unsigned char*& output, const size_t output_size, size_t& output_written_size) noexcept {
                uint32_t bitCnt = this->bit_buffer_pos;
                {
                    const uint32_t byteCnt = min(bitCnt / 8, output_size);
                    output_written_size = byteCnt;
                    if(byteCnt != 0) {
                        uint32_t bits = this->bit_buffer;

                        for(uint32_t i = 0; i < byteCnt; ++i) {
                            *output++ = bits >> 24;
                            bits = bits << 8;
                        }

                        this->bit_buffer = bits;
                        bitCnt -= byteCnt * 8;
                        this->bit_buffer_pos = bitCnt;
                    }
                }

                if(bitCnt < 8) [[likely]] {
                    return TAMP_OK;
                } else {
                    return TAMP_OUTPUT_FULL;
                }
            }


            constexpr void setWindowSizeLog2(const uint32_t sz) noexcept {
                this->conf_window = sz;
                this->window_size = (uint32_t)1 << sz;
            }

            constexpr uint32_t getWindowSizeLog2() const noexcept {
                return this->conf_window;
            }

            constexpr uint32_t windowSize() const noexcept {
                return this->window_size;
            }              

            constexpr uint32_t maxPatternLen() const noexcept {
                return this->min_pattern_size + MAX_PATTERN_RANGE - 1;
            }

            void addToWindow(const uint8_t* const data, const uint32_t len) noexcept {
                const uint32_t window_size = this->windowSize(); 
                const uint32_t l1 = min(len, window_size - this->window_pos);
                // assert 1 <= l1 && l1 <= INBUF_SIZE
                if(l1 < INBUF_SIZE) [[likely]] {
                    mem::cpy_short<INBUF_SIZE>(this->window + this->window_pos, data, l1);
                    // this->bufcpy(this->window + this->window_pos, data, l1);
                    if(len > l1) [[unlikely]] {
                        mem::cpy_short<INBUF_SIZE>(this->window, data+l1, len-l1);
                        // this->bufcpy(this->window, data+l1, len-l1);         
                    }
                } else {
                    mem::cpy_short<INBUF_SIZE>(this->window + this->window_pos, data);
                }
                this->window_pos = (this->window_pos + len) & (window_size - 1);    
            }        

            constexpr bool setLitLen(uint8_t n_bits) noexcept {
                if(n_bits == 0) {
                    n_bits = 8;
                } 
                if (LitLenTrait::isValidLitLen(n_bits)) {
                    LitLenTrait::setLiteralBits(*this, n_bits);
                    return true;
                } else {
                    return false;
                }
            }

            constexpr uint8_t getLitLen() const noexcept {
                return LitLenTrait::getLiteralBits(*this);
            }

            constexpr uint32_t getLiteralFlag() const noexcept {
                return (uint32_t)1 << getLitLen();
            }

            constexpr bool isValidLiteral(const uint32_t c) const noexcept {
                return !HAS_VAR_LITLEN || 
                    ((c >> getLitLen()) == 0);
                    // (c < getLiteralFlag());
            }


            // encodes [min_pattern_bytes, min_pattern_bytes + 13] pattern lengths
            // The bit lengths pre-add the 1 bit for the 0-value is_literal flag.
            static constexpr detail::huffcode_t huffman_codes[] {
                {0x0, 2},
                {0x3, 3},
                {0x8, 5},
                {0xb, 5},
                {0x14, 6},
                {0x24, 7},
                {0x26, 7},
                {0x2b, 7},
                {0x4b, 8},
                {0x54, 8},
                {0x94, 9},
                {0x95, 9},
                {0xaa, 9},
                {0x27, 7}
            };

            static constexpr uint32_t MAX_PATTERN_RANGE = std::extent_v<typeof(huffman_codes)>;    
    };

    static_assert(sizeof(Compressor) == sizeof(TampCompressor));

} // namespace tamp
