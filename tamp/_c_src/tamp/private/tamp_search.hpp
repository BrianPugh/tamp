/*

    Copyright 2024, <https://github.com/BitsForPeople>
     
    This program is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the
    Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    This program is distributed in the hope that it will be useful, bu
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License along
    with this program. If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once
#include <cstdint>
#include <type_traits>
#include <algorithm>

#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif

#include "tamp_arch.hpp"
#include "tamp_byte_span.hpp"

#include "tamp_stats.hpp"

#ifdef __has_cpp_attribute
    #if __has_cpp_attribute(assume)
        #define TAMP_ASSUME(x) [[assume(x)]]
    #endif
#endif
#ifndef TAMP_ASSUME
    #ifdef __has_builtin
        #if __has_builtin(__builtin_unreachable)
            #define TAMP_ASSUME(x) if(!(x)) { __builtin_unreachable(); }
        #endif
    #endif
#endif
#ifndef TAMP_ASSUME
    #define TAMP_ASSUME(x)
#endif


namespace tamp {

    /**
     * @brief Optimized functions for locating substring ("pattern") matches.
     *
     */
    class Locator {
        public:
            static constexpr bool STATS_ENABLED = false;

            // If enabled, provides statistics about search runs.
            inline static Stats<STATS_ENABLED> stats {};

            /**
             * @brief Searches \p data for the longest prefix of \p pattern that can be found.
             *
             * @param pattern
             * @param patLen length of the pattern; must be >= 2
             * @param data
             * @param dataLen
             * @return a std::span of the match found in data, or an empty std::span if no prefix was found.
             */
            static byte_span find_longest_match(
                const uint8_t* const pattern,
                const uint32_t patLen,
                const uint8_t* data,
                uint32_t dataLen) noexcept {

                const uint8_t* bestMatch {nullptr};
                uint32_t matchLen {0};

                {
                    const uint8_t* match;
                    uint32_t searchLen = 2;
                    uint32_t maxSearchLen = std::min(patLen,dataLen); // A match cannot be longer than the pattern OR extend beyond data
                    do {
                        match = find_pattern(pattern, searchLen, data, dataLen);
                        if(match) {
                            bestMatch = match;
                            matchLen = searchLen;
                            if(searchLen < maxSearchLen) [[likely]] {
                                // If the current match happens to extend beyond what we searched for,
                                // we'll take that too.
                                matchLen += cmp8(pattern+matchLen, match+matchLen, maxSearchLen-matchLen);
                            }
                            dataLen -= match+1-data;
                            data = match+1;

                            searchLen = matchLen+1; // Next match must beat the current one.

                            maxSearchLen = std::min(maxSearchLen,dataLen); // Towards the end of the search, the remaining dataLen may become less than patLen!
                        }
                    } while(match && searchLen <= maxSearchLen);
                }

                return byte_span {bestMatch,matchLen};

            }        


        private:

            /**
             * @brief Scalar, i.e. non-SIMD, pattern search.
             *
             * @param pattern start of pattern to search for
             * @param patLen length of pattern to search for
             * @param data start of data to search
             * @param dataLen length of data to search
             * @return first start of pattern in \p data, or \c nullptr if not found
             */
            static const uint8_t* find_pattern_scalar(const uint8_t* const pattern, const uint32_t patLen, const uint8_t* data, const uint32_t dataLen) noexcept {
                if(patLen > sizeof(uint32_t)) {
                    return find_pattern_long_scalar(pattern, patLen, data, dataLen);
                } else {
                    return find_pattern_short_scalar(pattern,patLen, data, dataLen);
                }
            }        

            /**
             * @brief Searches \p data for the first occurence of a \p pattern.
             * On ESP32-S3 targets, this uses SIMD instructions; delegates to ::find_pattern_scalar() on other targets.
             *
             * @param pattern start of pattern to search for
             * @param patLen length of pattern to search for
             * @param data start of data to search
             * @param dataLen length of data to search
             * @return first start of pattern in \p data, or \c nullptr if not found
             */
            static const uint8_t* __attribute__((noinline)) find_pattern(const uint8_t* const pattern, const uint32_t patLen, const uint8_t* const data, const uint32_t dataLen) noexcept {

                stats.enterFind(dataLen, patLen);

                if constexpr (Arch::ESP32S3) {
                    // Memory barrier for the compiler
                    asm volatile (""::"m" (*(const uint8_t(*)[dataLen])data));
                    asm volatile (""::"m" (*(const uint8_t(*)[patLen])pattern));

                    // q7[n] := POS_U8[n]
                    asm volatile (
                        "LD.QR q7, %[bytes]" "\n"
                        :
                        : [bytes] "m" (POS_U8)
                    );

                    // q4[n] := pattern[0]
                    asm volatile (
                        "EE.VLDBC.8 q4, %[first]"
                        :
                        : [first] "r" (pattern)
                    );

                    // q5[n] := pattern[patLen-1]
                    asm volatile (
                        "EE.VLDBC.8 q5, %[last]"
                        :
                        : [last] "r" (pattern + patLen - 1)
                    );


                    // Pointer to potential match on first byte of pattern:
                    const uint8_t* first = data;
                    // Pointer to potential match on last byte of pattern:
                    const uint8_t* last = first + patLen - 1;

                    const uint8_t* const end = first + dataLen - patLen + 1;


                    // Using (q1:q0) to load+align 'first' data,
                    // (q3:q2) to load+align 'last' data
                    asm volatile (
                        "EE.VLD.128.IP q0, %[first], 16" "\n"
                        "EE.LD.128.USAR.IP q1, %[first], 16" "\n"

                        "EE.VLD.128.IP q2, %[last], 16" "\n"
                        : [first] "+r" (first),
                          [last] "+r" (last)
                    );

                    const uint8_t* const pat1 = pattern+1;
                    // We won't need to compare the first and the last byte of pattern a second time.
                    const uint32_t cmpLen = patLen - std::min(patLen,(uint32_t)2);

                    // 'first' is ahead by 32 bytes of where we are actually searching.
                    const uint8_t* const flimit = end + 32;

                    do {

                        // Want at least one iteration while there is any data left.
                        uint32_t tmp = ((flimit - first) + 15) / 16;
                        asm volatile (

                            "EE.ZERO.ACCX" "\n"

                            "LOOPNEZ %[tmp], lend_%=" "\n"

                                /* Align & compare data from 'first' */
                                "EE.SRC.Q.QUP q1, q0, q1" "\n"

                                    // Pipelining: Load next data from 'last'
                                    "EE.LD.128.USAR.IP q3, %[last], 16" "\n" // Need USAR here!

                                // q6[n] := first[n] == pattern[0]
                                "EE.VCMP.EQ.S8 q6, q1, q4" "\n"

                                /* Align & compare data from 'last' */
                                "EE.SRC.Q.QUP q3, q2, q3" "\n"
                                // q3[n] := last[n] == pattern[patLen-1]
                                "EE.VCMP.EQ.S8 q3, q3, q5" "\n"

                                /* Fold: */
                                // ACCX += SUM( (int8_t)q6[n] * (int8_t)q3[n] ) 
                                // The comparisons above yield either 0 (false) or 0xff (true) for each lane.
                                // 0xff interpreted as signed 8-bit is -1.
                                // Thus, signed multiplication of the two comparison results is equivalent to a
                                // boolean AND, yielding either 0 or 1 (=(-1)*(-1)).
                                // Then accumulating these 16 multiplication results into ACCX yields the number 
                                // of 1's (0...16); ACCX := COUNT( q6[n] && q3[n] )
                                "EE.VMULAS.S8.ACCX q6, q3" "\n"

                                    // Pipelining: Load next data from 'first'
                                    "EE.LD.128.USAR.IP q1, %[first], 16" "\n" // Need USAR here!

                                // Get result, exit loop if any matches:
                                "RUR.ACCX_0 %[tmp]" "\n"
                                "BNEZ %[tmp], lend_%=" "\n"

                            "lend_%=:"
                            : [first] "+r" (first),
                              [last] "+r" (last),
                              [tmp] "+r" (tmp)
                            :
                        );

                        if(tmp) {
                            stats.potMatchesFound(tmp);

                            // The results of the comparisons are still in q6 and q3.
                            // Note: We must not disturb q0, q1, or q2 here, as their current contents are used in the next iteration.

                            /* Yes, it takes no more than 7 instructions, plus the 'lookup table' from POS_U8 (in q7),
                             * to extract 16 bits from the 16 8-bit boolean results of a vector comparison.
                             *
                             * Seriously. This is the fastest way I could come up with.
                             * The S3's PIE has only ONE horizontal SIMD instruction (VMULAS.ACCX).
                             */
                            asm volatile (

                                "EE.ZERO.ACCX" "\n"

                                // AND both comparison results
                                // q6[n] := q6[n] && q3[n]
                                "EE.ANDQ q6, q6, q3" "\n"                                

                                // And position factor with comparison mask:
                                // q6[n] := POS_U8[n] & q6[n]
                                "EE.ANDQ q6, q7, q6" "\n"

                                // MAC 1: MAC all (non-masked) elements:
                                "EE.VMULAS.U8.ACCX q6, q6" "\n"

                                // Extract only the even elements from q6, set the rest to 0.
                                "EE.ZERO.Q q3" "\n" // Get zeros to fill the gaps.
                                "EE.VUNZIP.8 q6, q3" "\n"

                                // MAC the even elements one more time:
                                "EE.VMULAS.U8.ACCX q6, q6" "\n"

                                // Extract:
                                "RUR.ACCX_0 %[tmp]" "\n"

                                :
                                    [tmp] "=r" (tmp)
                                :
                            );

                            // What we want is now in bits 15...0 of tmp, where bit i denotes if
                            // (first[15-i] == pattern[0] && last[15-i] == pattern[patLen-1])

                            tmp = tmp << 16;

                            TAMP_ASSUME(tmp != 0);                            

                            const uint8_t* s1 = first-48;

                            // Now iterate over all '1' bits in tmp from MSB to LSB
                            // In practice, tmp is usually very sparsely populated (1-2 bits set)
                            // at this point. (For purely random data, we'd expect each bit
                            // in tmp to be set with an independent probability of 1/65536.)
                            do {
                                {
                                    const uint32_t bits = __builtin_clz(tmp) + 1;
                                    s1 += bits;
                                    tmp = tmp << bits; // Remove the bit we're handling now.
                                }
                                if(s1 <= end) [[likely]] {
                                    if(cmpLen == 0 || cmp8(s1,pat1,cmpLen) >= cmpLen) {

                                        stats.matchFound(patLen, (s1-1)-data + patLen);

                                        return s1-1;
                                    }
                                } else {
                                    // Found match would extend beyond the end of data.
                                    goto not_found;
                                }

                            } while(tmp != 0);
                        }
                    } while(first < flimit);

                    not_found:

                    stats.noMatchFound(dataLen);

                    return nullptr;

                } else {
                    return find_pattern_scalar(pattern, patLen, data, dataLen);
                }

            }

            template<int32_t INC, typename T>
            static void __attribute__((always_inline)) incptr(T*& ptr) noexcept {
                ptr = (T*)(p<uint8_t>(ptr) + INC);
            }

            template<typename T>
            static const T* p(const void* const ptr) noexcept {
                return (const T*)ptr;
            }

            template<typename T>
            static T __attribute__((always_inline)) as(const void* const ptr) noexcept {
                return *(const T*)(ptr);
            }

            /**
             * @brief Returns \p x rounded down to the next lower multiple of \p M.
             * 
             * @tparam M 
             * @param x 
             * @return greatest integer multiple of \p M <= \p x
             */
            template<uint32_t M>
            static uint32_t multof(const uint32_t x) noexcept {
                static_assert(M!=0);
                if constexpr (M==1) {
                    return x;
                } else
                if constexpr ((M & (M-1)) == 0) {
                    return x & ~(M-1);
                } else {
                    return (x / M) * M;
                }
            }            

            template<typename T, uint32_t M, uint32_t N = 0>
            static bool __attribute__((always_inline)) unrolled_find(const uint8_t*& data, const uint32_t v) noexcept {
                if(as<T>(data+N) != v) [[likely]] {
                    if constexpr (N+1 < M) {
                        return unrolled_find<T,M,N+1>(data,v);
                    } else {
                        return false;
                    }
                } else {
                    incptr<N>(data);
                    return true;
                }
            }


            template<uint32_t M, uint32_t N = 0>
            static bool __attribute__((always_inline)) unrolled_find_3(const uint8_t*& data, const uint32_t vl, const uint32_t vh) noexcept {
                /* Loading one uint32_t from data, we compare
                 * (*(uint32_t*)data << 8) against vl and
                 * (*(uint32_t*)data >> 8) against vh
                 * This turns out to be slightly faster than loading (*(uint32_t*)data) and (*(uint32_t*)(data+1)),
                 * and reduces register pressure on Xtensas because L32I requires the offset to be a multiple of 4.
                */
                static_assert(N < M);
                if constexpr (N % 2 == 0) {
                    if((as<uint32_t>(data+((N/2)*2))<<8) != vl) [[likely]] {
                        if constexpr (N+1 < M) {
                            return unrolled_find_3<M,N+1>(data,vl,vh);
                        } else {
                            return false;
                        }
                    } else {
                        incptr<N>(data);
                        return true;
                    }
                } else {
                    if((as<uint32_t>(data+((N/2)*2))>>8) != vh) [[likely]] {
                        if constexpr (N+1 < M) {
                            return unrolled_find_3<M,N+1>(data,vl,vh);
                        } else {
                            return false;
                        }
                    } else {
                        incptr<N>(data);
                        return true;
                    }
                }
            }            
            /**
             * @brief Scalar pattern search for patterns <= sizeof(uint32_t) bytes in length.
             *
             * @param pattern
             * @param patLen must be 2, 3, or 4!
             * @param data
             * @param dataLen
             * @return start of the first match found in \p data, or \c nullptr if none found.
             */
            static const uint8_t* /* __attribute__((noinline)) */ find_pattern_short_scalar(const uint8_t* const pattern, const uint32_t patLen, const uint8_t* data, uint32_t dataLen) noexcept {
                const uint8_t* const end = data + dataLen - (patLen-1);

                // Memory barrier for the compiler.
                asm (""::"m" (*(const uint8_t(*)[dataLen])data));

                if(patLen == 2) [[likely]] {
                    // This is where ~50% of the total CPU time is spent!

                    const uint32_t v = as<uint16_t>(pattern);

                    if constexpr (Arch::XTENSA && Arch::XT_LOOP) {

                        uint32_t tmp_32; // 32-bit value loaded from memory
                        uint32_t tmp_16; // 16-bit value extracted from tmp_32

                        const uint32_t cnt = ((dataLen-(sizeof(uint16_t)-1))+2)/3; // We're processing 3 locations per iteration.

                        asm (
                            "LOOPNEZ %[cnt], lend_%=" "\n"

                                "L32I %[tmp_32], %[data], 0" "\n"
                                "ADDI %[data], %[data], 3" "\n"  // Pipelining.

                                "EXTUI %[tmp_16], %[tmp_32], 0, 16" "\n" // extract bits 15...0
                                "BEQ %[tmp_16], %[v], found_0_%=" "\n" // -> found at data-3

                                "EXTUI %[tmp_16], %[tmp_32], 8, 16" "\n" // extract bits 23...8
                                "BEQ %[tmp_16], %[v], found_1_%=" "\n" // -> found at data-2

                                "EXTUI %[tmp_16], %[tmp_32], 16, 16" "\n" // extract bits 31...16
                                "BEQ %[tmp_16], %[v], found_2_%=" "\n" // -> found at data-1

                            "lend_%=:" "\n"
                                // Nothing found, data >= end.
                                "J exit_%=" "\n"

                            "found_0_%=:" "\n"
                                "ADDI %[data], %[data], -1" "\n"

                            "found_1_%=:" "\n"
                                "ADDI %[data], %[data], -1" "\n"

                            "found_2_%=:" "\n"
                                "ADDI %[data], %[data], -1" "\n"

                            "exit_%=:"
                            :
                                [tmp_32] "=&r"(tmp_32),
                                [tmp_16] "=&r" (tmp_16),
                                [data] "+&r"(data)
                            : 
                                [v] "r"(v),
                                [cnt] "r"(cnt)
                        );
                        
                    } else {

                        constexpr uint32_t LOOP_UNROLL_FACTOR = 8;

                        // Loop unrolled 8x.
                        const uint8_t* const end_unrolled = data + multof<LOOP_UNROLL_FACTOR>(dataLen-(patLen-1));

                        while(data < end_unrolled && !unrolled_find<uint16_t,LOOP_UNROLL_FACTOR>(data,v)) [[likely]] {
                            incptr<LOOP_UNROLL_FACTOR>(data);
                        }

                        if(data >= end_unrolled) { 
                            // Nothing found so far.
                            while(data < end && as<uint16_t>(data) != v) {
                                incptr<1>(data);
                            }
                        }
                    }
                } else
                if(patLen == 3) [[likely]] {

                    // Given a 24-bit value in a 32-bit variable V and 4 bytes (32 bits) loaded from memory
                    // in a 32-bit variable D,
                    // if ((V << 8) == (D << 8)) then the lower 3 bytes (24 bits) of D are equal to V,
                    // if (V == (D >> 8)) then the upper 3 bytes of D are equal to V.


                    const uint32_t vl = as<uint32_t>(pattern) << 8; 
                    const uint32_t vh = vl >> 8;

                    if constexpr (Arch::XTENSA && Arch::XT_LOOP) {

                        uint32_t tmp_32; // 32-bit value loaded from memory
                        uint32_t tmp_24; // 24-bit value extracted from tmp_32
                        const uint32_t len = end-data;

                        asm (
                            "LOOPNEZ %[cnt], lend_%=" "\n"

                                "L32I %[tmp_32], %[data], 0" "\n"
                                "ADDI %[data], %[data], 2" "\n" // Pipelining.

                                "SLLI %[tmp_24], %[tmp_32], 8" "\n"
                                "BEQ %[tmp_24], %[vl], found_lo_%=" "\n"

                                "SRLI %[tmp_24], %[tmp_32], 8" "\n"
                                "BEQ %[tmp_24], %[vh], found_hi_%=" "\n"

                            "lend_%=:" "\n"
                                "ADDI %[data], %[data], 2" "\n" // Make sure data points to end or end+1. (We need the check at the end anyway in case we overread and found_hi a match one byte too late.)
                            "found_lo_%=:" "\n"
                                "ADDI %[data], %[data], -1" "\n"
                            "found_hi_%=:" "\n"
                                "ADDI %[data], %[data], -1" "\n"
                            "exit_%=:" "\n"
                            :
                                [tmp_32] "=&r" (tmp_32),
                                [tmp_24] "=&r" (tmp_24),
                                [data] "+&r" (data)
                            : 
                                [vl] "r" (vl),
                                [vh] "r" (vh),
                                [cnt] "r" ((len+1)/2)
                        );

                    } else {

                        constexpr uint32_t LOOP_UNROLL_FACTOR = 8;

                        // Loop unrolled 8x.
                        const uint8_t* const end_unrolled = data + multof<LOOP_UNROLL_FACTOR>(dataLen-(patLen-1));

                        while(data < end_unrolled && !unrolled_find_3<LOOP_UNROLL_FACTOR>(data,vl,vh)) [[likely]] {
                            incptr<LOOP_UNROLL_FACTOR>(data);
                        }
                                                    
                        if(data >= end_unrolled) { 
                            // Nothing found so far.
                            while(data < end && (as<uint32_t>(data) << 8) != vl) {
                                incptr<1>(data);
                            }
                        } 
                    }

                } else {
                    // assert patLen == 4
                    const uint32_t v = as<uint32_t>(pattern);

                    if constexpr (Arch::XTENSA && Arch::XT_LOOP) {

                        // As of gcc 14.2.0, the compiler can be persuaded to generate the same code.
                        uint32_t tmp;

                        asm (
                            "LOOPNEZ %[len], lend_%=" "\n"
                                "L32I %[tmp], %[data], 0" "\n"
                                "ADDI %[data], %[data], 1" "\n" // Pipelining.
                                "BEQ %[tmp], %[v], found_%=" "\n"
                            "lend_%=:" "\n"
                                "MOVI %[data], 1" "\n" // Make result = 0 w/o a jump
                            "found_%=:" "\n"
                                "ADDI %[data], %[data], -1" "\n"
                            "exit_%=:"
                            :
                                [tmp] "=&r" (tmp),
                                [data] "+&r" (data)
                            : 
                                [v] "r" (v),
                                [len] "r" (dataLen-(sizeof(uint32_t)-1))
                        );                            

                        return data;
                        
                    } else {

                        constexpr uint32_t LOOP_UNROLL_FACTOR = 8;

                        const uint8_t* const end_unrolled = data + multof<LOOP_UNROLL_FACTOR>(dataLen-(patLen-1));
                        while(data < end_unrolled && !unrolled_find<uint32_t,LOOP_UNROLL_FACTOR>(data,v)) [[likely]] {
                            incptr<LOOP_UNROLL_FACTOR>(data);
                        }

                        if(data >= end_unrolled) { 
                            // Nothing found so far.
                            while(data < end && as<uint32_t>(data) != v) {
                                incptr<1>(data);
                            }
                        }   
                    }

                }

                return (data < end) ? data : nullptr;
            }


            // template<uint32_t M, uint32_t N = 0>
            // static bool __attribute__((always_inline)) _unrolled_find_f_l(const uint8_t*& first, const uint8_t*& last, const uint32_t f, const uint32_t l) noexcept {
            //     const uint8_t* fp = first;
            //     const uint8_t* const end = fp + M;
            //     const uint32_t len = &last - &first;
            //     do {
            //         const uint8_t* ff = find_pattern_short_scalar((const uint8_t*)&f,sizeof(uint32_t),fp,end-fp);
            //         if(ff) {
            //             if(*(const uint32_t*)(ff+len) != l) [[likely]] {
            //                 fp = ff + 1;
            //             } else {
            //                 first = ff;
            //                 last = ff+len;
            //                 return true;
            //             }
            //         } else {
            //             return false;
            //         }
            //     } while(fp < end);

            //     return false;
            // }            

            /**
             * @brief Scalar pattern search for patterns > sizeof(uint32_t) bytes in length.
             *
             * @param pattern
             * @param patLen must be >= sizeof(uint32_t)
             * @param data
             * @param dataLen
             * @return start of the first match found in \p data, or \c nullptr if none found.
             */
            // Attention: For some reason, when gcc inlines this, sometimes this DOUBLES the total processing time. Needs investigation!
            static const uint8_t* /* __attribute__((noinline)) */ find_pattern_long_scalar(const uint8_t* pattern, const uint32_t patLen, const uint8_t* data, uint32_t dataLen) noexcept {
                using T = uint32_t;
                constexpr uint32_t sw = sizeof(T);

                const uint8_t* first = data;
                const uint8_t* last = first + patLen-sw;
                const uint32_t f = as<T>(pattern);
                const uint32_t l = as<T>(pattern + patLen - sw);

                const uint8_t* const end = first + dataLen - (patLen-1);

                const uint32_t cmpLen = patLen - std::min(patLen,2*sw);

                do {
                    {
                        constexpr uint32_t LOOP_UNROLL_FACTOR = 8;
                        const uint8_t* const end_unrolled = first + multof<LOOP_UNROLL_FACTOR>(end-first);
                        while(first < end_unrolled && !unrolled_find_f_l<LOOP_UNROLL_FACTOR>(first,last,f,l)) [[likely]] {
                            incptr<LOOP_UNROLL_FACTOR>(first);
                            incptr<LOOP_UNROLL_FACTOR>(last);
                        }

                        if(first >= end_unrolled) {
                            // Nothing found so far.
                            while(first < end) {
                                if(f == as<T>(first) && l == as<T>(last)) {
                                    break;
                                } else {
                                    incptr<1>(first);
                                    incptr<1>(last);
                                }
                            };
                        }
                    }

                    if(first < end) {
                        if(cmpLen == 0 || cmp8(first+sw,pattern+sw,cmpLen) >= cmpLen) {
                            return first;
                        } else {
                            incptr<1>(first);
                            incptr<1>(last);
                        }
                    }
                } while (first < end);

                return nullptr;
            }

            // Unrolled loop generator for find_pattern_long_scalar.
            template<uint32_t M, uint32_t N = 0>
            static bool __attribute__((always_inline)) unrolled_find_f_l(const uint8_t*& first, const uint8_t*& last, const uint32_t f, const uint32_t l) noexcept {
                if(f != as<uint32_t>(first+N) || l != as<uint32_t>(last+N)) [[likely]] {
                    if constexpr (N+1 < M) {
                        return unrolled_find_f_l<M,N+1>(first,last,f,l);
                    } else {
                        return false;
                    }
                } else {
                    incptr<N>(first);
                    incptr<N>(last);
                    return true;
                }
            }            

            template<uint32_t M, uint32_t N = 0, typename P>
            static bool __attribute__((always_inline)) unrolled_cmp8(const P*& d1, const P*& d2) noexcept {
                if(*(p<uint8_t>(d1)+N) == *(p<uint8_t>(d2)+N)) [[likely]] {
                    if constexpr(N+1 < M) {
                        return unrolled_cmp8<M,N+1>(d1,d2);
                    } else {
                        return true;
                    }
                } else {
                    incptr<N>(d1);
                    incptr<N>(d2);
                    return false;
                }
            }

        // public:

            /**
             * @brief Memory compare, like ::cmp(), but only compares single bytes in a loop.
             * This is faster on average if a mismatch within the first few (~4-6 or so) bytes is likely.
             *
             * @param d1
             * @param d2
             * @param len must be >= 1
             * @return uint32_t
             */
            static uint32_t cmp8(const void* d1, const void* d2, const uint32_t len) noexcept {

                const uint8_t* const start = p<uint8_t>(d1);

                if constexpr (Arch::XTENSA && Arch::XT_LOOP) {
                    // Not really needed. GCC 12.2.0 generates the (ZOL) code for this too.

                    // Memory barrier for the compiler
                    asm volatile (""::"m" (*(const uint8_t(*)[len])d1));
                    asm volatile (""::"m" (*(const uint8_t(*)[len])d2));

                    uint32_t tmp1, tmp2;
                    asm volatile (
                        "LOOPNEZ %[cnt], lend_%=" "\n"

                            "L8UI %[tmp2], %[d2], 0" "\n"
                            "L8UI %[tmp1], %[d1], 0" "\n"

                            "ADDI %[d2], %[d2], 1" "\n" // Pipelining.

                            "BNE %[tmp1], %[tmp2], lend_%=" "\n"

                            "ADDI %[d1], %[d1], 1" "\n"                            

                        "lend_%=:"
                        :
                          [tmp1] "=r" (tmp1),
                          [tmp2] "=r" (tmp2),
                          [d1] "+r" (d1),
                          [d2] "+r" (d2)
                        :
                          [cnt] "r" (len)
                    );

                    return p<uint8_t>(d1)-start;                    

                } else {
                    const uint8_t* const end = p<uint8_t>(d1) + len;                    
                    
                    while(d1 < end) {
                        if(as<uint8_t>(d1) == as<uint8_t>(d2)) [[likely]] {
                            incptr<1>(d1);
                            incptr<1>(d2);
                        } else {
                            break;
                        }
                    }
                    return p<uint8_t>(d1)-start;                    
                }

            }


            /*
              Challenge: Given a vector of 16 8-bit booleans (0x00/0xff), find the
              position (0...15) of every 0xff in that vector.
              
              We do this by mapping each position i to the 16-bit power-of-two 1<<i,
              performing a bit-wise AND of each 16-bit value with the corresponding
              8-bit boolean, and summing up all 16 16-bit values into one 16-bit
              value. On this value, we then use Count Leading Zeros (CLZ) to iterate
              over all '1' bits.

              Mapping the 8-bit values to 16-bit values isn't straight forward though,
              because we have only basic arithmetic operations on either 8- *or* 16-bit
              values at our disposal. So to go from 16x8 to 16x16 to 1x16 bits (fast),
              some trickery is needed.

              The ACCX register is 39+1 bits wide and lets us accumulate unsigned 8-bit
              values into a single result of up to 39 bits, i.e. enough for the 16 bits
              we need.


              After masking with the 8-bit booleans, we multiply (mac) this vector with
              itself, then mac the values at all even positions again which makes the
              even positions 'worth' 2x as much, so we effectively get an 8x16 mac with
              {2*(128*128),(128*128),2*(64*64),(64*64),...,2*(1*1),(1*1)}, i.e.
              {(1<<15),(1<<14),(1<<13),(1<<12),...,(1<<1),(1<<0)} as desired.

              Positions in the vector are mapped to bits in descending order, the first
              u8 to 1<<15, the last u8 to 1<<0. This is because the Xtensas have a 1-cycle
              "count leading zeroes" (CLZ) instruction ("NSAU"), whereas a "count trailing
              zeroes" (CTZ) or "find first set" (FFS) requires ~4 instructions.
             */
            /**
             * @brief Vector of \c uint8_t used for extracting match positions when
             * using ESP32-S3 SIMD.
             */
            alignas(16) static constexpr uint8_t POS_U8[16] {
                128,128,64,64,32,32,16,16,8,8,4,4,2,2,1,1,
            };



    };

} // namespace tamp