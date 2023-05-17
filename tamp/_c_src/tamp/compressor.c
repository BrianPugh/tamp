#include "compressor.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))

#define MAX_PATTERN_SIZE (compressor->min_pattern_size + 13)
#define WINDOW_SIZE (1 << compressor->conf.window)
// 0xF because sizeof(TampCompressor.input) == 16;
#define input_add(offset) (\
            (compressor->input_pos + offset) & 0xF \
        )
#define read_input(offset) ( \
        compressor->input[input_add(offset)] \
        )
#define IS_LITERAL_FLAG (1 << compressor->conf.literal)

#define FLUSH_CODE (0xAB)

// encodes [min_pattern_bytes, min_pattern_bytes + 13] pattern lengths
static const unsigned char huffman_codes[] = {
    0x0, 0x3, 0x8, 0xb, 0x14, 0x24, 0x26, 0x2b, 0x4b, 0x54, 0x94, 0x95, 0xaa, 0x27
};
// These bit lengths pre-add the 1 bit for the 0-value is_literal flag.
static const uint8_t huffman_bits[] = {
    0x2, 0x3, 0x5, 0x5, 0x6, 0x7, 0x7, 0x7, 0x8, 0x8, 0x9, 0x9, 0x9, 0x7
};

static inline void write_to_bit_buffer(TampCompressor *compressor, uint32_t bits, uint8_t n_bits){
    compressor->bit_buffer_pos += n_bits;
    compressor->bit_buffer |= bits << (32 - compressor->bit_buffer_pos);
}

/**
 * @brief Partially flush the internal bit buffer.
 *
 * Up to 7 bits may remain in the internal bit buffer.
 */
static inline tamp_res partial_flush(TampCompressor *compressor, unsigned char *output, size_t output_size, size_t *output_written_size){
    if(output_written_size)
        *output_written_size = 0;

    while(compressor->bit_buffer_pos >= 8 && output_size){
        *output = compressor->bit_buffer >> 24;
        output++;
        compressor->bit_buffer <<= 8;
        if(output_written_size)
            (*output_written_size)++;
        output_size--;
        compressor->bit_buffer_pos -= 8;
    }
    if(compressor->bit_buffer_pos >= 8)
        return TAMP_OUTPUT_FULL;
    return TAMP_OK;
}


static uint8_t single_search(
        TampCompressor *compressor,
        uint16_t window_size,
        uint16_t window_offset,
        uint8_t input_offset
        ){
    if(window_offset + MAX_PATTERN_SIZE < window_size && compressor->input_size >= MAX_PATTERN_SIZE){
        /* Common "Happy" case unrolled */
        if(compressor->window[window_offset++] != read_input(input_offset))
            return input_offset;
        if(compressor->window[window_offset++] != read_input(++input_offset))
            return input_offset;
        if(compressor->window[window_offset++] != read_input(++input_offset))
            return input_offset;
        if(compressor->window[window_offset++] != read_input(++input_offset))
            return input_offset;
        if(compressor->window[window_offset++] != read_input(++input_offset))
            return input_offset;
        if(compressor->window[window_offset++] != read_input(++input_offset))
            return input_offset;
        if(compressor->window[window_offset++] != read_input(++input_offset))
            return input_offset;
        if(compressor->window[window_offset++] != read_input(++input_offset))
            return input_offset;
        input_offset++;
        for(;
            input_offset < MAX_PATTERN_SIZE;
            input_offset++, window_offset++
        ){
            if(compressor->window[window_offset] != read_input(input_offset))
                break;
        }
    }
    else{
        for(;
            input_offset < compressor->input_size && window_offset < window_size && input_offset < MAX_PATTERN_SIZE;
            input_offset++, window_offset++
        ){
            if(compressor->window[window_offset] != read_input(input_offset))
                break;
        }
    }
    return input_offset;
}

/**
 * @brief Find the best match for the current input buffer.
 *
 * WARNING: this optimized implementation expects a little endian system.
 *
 * @param[in,out] compressor TampCompressor object to perform search on.
 * @param[out] match_index  If match_size is 0, this value is undefined.
 * @param[out] match_size Size of best found match.
 */
static void find_best_match(
        TampCompressor *compressor,
        uint16_t *match_index,
        uint8_t *match_size
        ){
    const uint32_t *window_uint32_ptr = (uint32_t *)compressor->window;
    const uint16_t window_size_uint32 = (1 << (compressor->conf.window - 2));
    const uint16_t window_size = (1 << compressor->conf.window);
    const uint8_t mask8 = 0xFF;
    const uint16_t mask16 = 0xFFFF;
    const unsigned char first_c = read_input(0);
    const uint16_t first_second_c = (read_input(1) << 8) | first_c;
    const unsigned char third_c = read_input(2);
    const unsigned char fourth_c = read_input(3);
    uint8_t proposed_match_size;

    *match_size = 0;

    if(compressor->input_size < compressor->min_pattern_size)
        return;

    for (uint16_t i=0; i < window_size_uint32; i++){
        // WARNING: this is all little-endian
        uint32_t c32 = window_uint32_ptr[i];
        if((c32 & mask16) == first_second_c){
            if(((c32 >> 16) & mask8) == third_c && compressor->input_size >= 3){
                if(((c32 >> 24) & mask8) == fourth_c && compressor->input_size >= 4)
                    proposed_match_size = single_search(compressor, window_size, (i + 1) << 2, 4);
                else
                    proposed_match_size = 3;
            }
            else{
                proposed_match_size = 2;
            }
            if(proposed_match_size > *match_size){
                *match_size = proposed_match_size;
                *match_index = i << 2;
                if(*match_size == MAX_PATTERN_SIZE || *match_size == compressor->input_size)
                    return;
            }
        }
        if(((c32 >> 8) & mask16) == first_second_c){
            if(((c32 >> 24) & mask8) == third_c && compressor->input_size >= 3)
                proposed_match_size = single_search(compressor, window_size, (i + 1) << 2, 3);
            else
                proposed_match_size = 2;
            if(proposed_match_size > *match_size){
                *match_size = proposed_match_size;
                *match_index = (i << 2) + 1;
                if(*match_size == MAX_PATTERN_SIZE || *match_size == compressor->input_size)
                    return;
            }
        }
        if(((c32 >> 16) & mask16) == first_second_c){
            proposed_match_size = single_search(compressor, window_size, (i + 1) << 2, 2);
            if(proposed_match_size > *match_size){
                *match_size = proposed_match_size;
                *match_index = (i << 2) + 2;
                if(*match_size == MAX_PATTERN_SIZE || *match_size == compressor->input_size)
                    return;
            }
        }
        if((c32 >> 24) == first_c){
            proposed_match_size = single_search(compressor, window_size, (i + 1) << 2, 1);
            if(proposed_match_size > *match_size){
                *match_size = proposed_match_size;
                *match_index = (i << 2) + 3;
                if(*match_size == MAX_PATTERN_SIZE || *match_size == compressor->input_size)
                    return;
            }
        }
    }
}


tamp_res tamp_compressor_init(TampCompressor *compressor, const TampConf *conf, unsigned char *window){
    const TampConf conf_default = {.window=10, .literal=8, .use_custom_dictionary=false};
    if(!conf)
        conf = &conf_default;
    if( conf->window < 8 || conf->window > 15)
        return TAMP_INVALID_CONF;

    if( conf->literal < 5 || conf->literal > 8)
        return TAMP_INVALID_CONF;

    memcpy(&compressor->conf, conf, sizeof(TampConf));

    compressor->window = window;
    compressor->bit_buffer = 0;
    compressor->bit_buffer_pos = 0;
    compressor->min_pattern_size = tamp_compute_min_pattern_size(conf->window, conf->literal);
    compressor->input_size = 0;
    compressor->input_pos = 0;
    compressor->window_pos = 0;

    if(!compressor->conf.use_custom_dictionary)
        tamp_initialize_dictionary(window, (1 << conf->window), 3758097560);

    // Write header to bit buffer
    write_to_bit_buffer(compressor, conf->window - 8, 3);
    write_to_bit_buffer(compressor, conf->literal - 5, 2);
    write_to_bit_buffer(compressor, conf->use_custom_dictionary, 1);
    write_to_bit_buffer(compressor, 0, 1);  // Reserved
    write_to_bit_buffer(compressor, 0, 1);  // No more header bytes

    return TAMP_OK;
}


tamp_res tamp_compressor_compress_poll(TampCompressor *compressor, unsigned char *output, size_t output_size, size_t *output_written_size){
    tamp_res res;
    const uint16_t window_mask = (1 << compressor->conf.window) - 1;

    if(output_written_size)
        *output_written_size = 0;

    if(compressor->input_size == 0)
        return TAMP_OK;

    {
        // Make sure there's enough room in the bit buffer.
        size_t flush_bytes_written;
        res = partial_flush(compressor, output, output_size, &flush_bytes_written);
        if(output_written_size)
            (*output_written_size) += flush_bytes_written;
        if(res != TAMP_OK)
            return res;
        output_size -= flush_bytes_written;
        output += flush_bytes_written;
    }

    if(output_size == 0)
        return TAMP_OUTPUT_FULL;

    uint8_t match_size = 0;
    uint16_t match_index = 0;
    find_best_match(compressor, &match_index, &match_size);

    if(match_size < compressor->min_pattern_size){
        // Write LITERAL
        match_size = 1;
        unsigned char c = read_input(0);
        if(c >> compressor->conf.literal){
            return TAMP_EXCESS_BITS;
        }
        write_to_bit_buffer(compressor, IS_LITERAL_FLAG | c, compressor->conf.literal + 1);
    }
    else{
        // Write TOKEN
        uint8_t huffman_index = match_size - compressor->min_pattern_size;
        write_to_bit_buffer(compressor, huffman_codes[huffman_index], huffman_bits[huffman_index]);
        write_to_bit_buffer(compressor, match_index, compressor->conf.window);
    }
    // Populate Window
    for(uint8_t i=0; i < match_size; i++){
        compressor->window[compressor->window_pos] = read_input(0);
        compressor->window_pos = (compressor->window_pos + 1) & window_mask;
        compressor->input_pos = input_add(1);
        compressor->input_size--;
    }

    return TAMP_OK;
}


void tamp_compressor_sink(
        TampCompressor *compressor,
        const unsigned char *input,
        size_t input_size,
        size_t *consumed_size
        ){
    if(consumed_size)
        *consumed_size = 0;

    for(size_t i=0; i < input_size; i++){
        if(compressor->input_size == sizeof(compressor->input))
            break;
        compressor->input[input_add(compressor->input_size)] = input[i];
        compressor->input_size += 1;
        if(consumed_size)
            (*consumed_size)++;
    }
}

tamp_res tamp_compressor_compress(
        TampCompressor *compressor,
        unsigned char *output,
        size_t output_size,
        size_t *output_written_size,
        const unsigned char *input,
        size_t input_size,
        size_t *input_consumed_size
        ){
    tamp_res res;

    if(output_written_size)
        *output_written_size = 0;
    if(input_consumed_size)
        *input_consumed_size = 0;

    while(input_size > 0 && output_size > 0){
        {
            // Sink Data into input buffer.
            size_t consumed;
            tamp_compressor_sink(compressor, input, input_size, &consumed);
            input += consumed;
            input_size -= consumed;
            if(input_consumed_size)
                (*input_consumed_size) += consumed;
        }
        if(compressor->input_size == sizeof(compressor->input)){
            // Input buffer is full and ready to start compressing.
            size_t chunk_output_written_size;
            res = tamp_compressor_compress_poll(compressor, output, output_size, &chunk_output_written_size);
            output += chunk_output_written_size;
            output_size -= chunk_output_written_size;
            if(output_written_size)
                (*output_written_size) += chunk_output_written_size;
            if(res != TAMP_OK)
                return res;
        }
    }
    return TAMP_OK;
}

tamp_res tamp_compressor_flush(
        TampCompressor *compressor,
        unsigned char *output,
        size_t output_size,
        size_t *output_written_size,
        bool write_token
        ){
    tamp_res res;
    size_t chunk_output_written_size;

    if(output_written_size)
        *output_written_size = 0;

    while(compressor->input_size){
        // Compress the remainder of the input buffer.
        res = tamp_compressor_compress_poll(compressor, output, output_size, &chunk_output_written_size);
        if(output_written_size)
            (*output_written_size) += chunk_output_written_size;
        if(res != TAMP_OK)
            return res;
        output_size -= chunk_output_written_size;
        output += chunk_output_written_size;
    }

    // Perform partial flush to see if we need a FLUSH token, and to subsequently
    // make room for the FLUSH token.
    res = partial_flush(compressor, output, output_size, &chunk_output_written_size);
    output_size -= chunk_output_written_size;
    if(output_written_size)
        (*output_written_size) += chunk_output_written_size;
    output += chunk_output_written_size;
    if(res != TAMP_OK)
        return res;

    // Maybe write the FLUSH token
    if(compressor->bit_buffer_pos && write_token)
        write_to_bit_buffer(compressor, FLUSH_CODE, 9);

    // Flush the remainder of the output bit-buffer
    while(compressor->bit_buffer_pos && output_size){
        *output = compressor->bit_buffer >> 24;
        output++;
        compressor->bit_buffer <<= 8;
        compressor->bit_buffer_pos -= MIN(compressor->bit_buffer_pos, 8);
        output_size--;
        if(output_written_size)
            (*output_written_size)++;
    }

    if(compressor->bit_buffer_pos)  // There was not enough room in the output buffer to fully flush.
        return TAMP_OUTPUT_FULL;

    return TAMP_OK;
}

tamp_res tamp_compressor_compress_and_flush(
        TampCompressor *compressor,
        unsigned char *output,
        size_t output_size,
        size_t *output_written_size,
        const unsigned char *input,
        size_t input_size,
        size_t *input_consumed_size
        ){
    tamp_res res;
    size_t flush_size;
    size_t output_written_size_proxy;

    if(!output_written_size)
        output_written_size = &output_written_size_proxy;

    res = tamp_compressor_compress(
            compressor,
            output,
            output_size,
            output_written_size,
            input,
            input_size,
            input_consumed_size
            );
    if(res != TAMP_OK)
        return res;

    res = tamp_compressor_flush(
            compressor,
            output + *output_written_size,
            output_size - *output_written_size,
            &flush_size,
            false
            );

    (*output_written_size) += flush_size;

    if(res != TAMP_OK)
        return res;

    return TAMP_OK;
}
