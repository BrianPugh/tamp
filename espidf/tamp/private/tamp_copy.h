#ifndef TAMP_ESP32_TAMP_COPY_H
#define TAMP_ESP32_TAMP_COPY_H

/* ESP32 (TAMP_ESP32) copy fast paths for the decompressor hot loops; included
 * by decompressor.c under TAMP_ESP32. Word-at-a-time copy idea from
 * BitsForPeople/esp-tamp (PR #2), reworked for tamp's wrap-aware overlap
 * semantics and exact-length reads.
 *
 * The unaligned word accesses are deliberate type-punning. ESP32-family cores
 * handle unaligned 32-bit data accesses in hardware, and every buffer touched
 * here (window, user output) is data RAM. may_alias covers strict-aliasing;
 * alignment-wise this is technically undefined C, but every conforming
 * alternative defeats the optimization: xtensa GCC treats the target as
 * strict-alignment, so memcpy-based loads, aligned(1) typedefs, and packed
 * structs all lower to four byte loads + four byte stores (measured with
 * xtensa-esp32s3-elf-gcc 15.2 -O3), while direct punning keeps the l32i.n
 * zero-overhead loop. Same technique as the SWAR scan in compressor.c and
 * tamp_search.hpp. Host-side fuzzing compiles this path with
 * -fno-strict-aliasing -fno-sanitize=alignment (see esp32-host-test).
 */

#include <stdint.h>

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t __attribute__((may_alias)) tamp_ua_u32;
typedef uint16_t __attribute__((may_alias)) tamp_ua_u16;

/**
 * @brief Forward word-at-a-time copy of exactly @p count bytes.
 *
 * Each chunk is fully loaded before it is stored, and chunks advance forward,
 * so the copy is also safe when the regions overlap with dst < src (bytes are
 * read before they are overwritten). NOT safe for dst > src overlap.
 */
static inline void tamp_copy_fwd(unsigned char *dst, const unsigned char *src, uint32_t count) {
    while (count >= 4) {
        *(tamp_ua_u32 *)dst = *(const tamp_ua_u32 *)src;
        dst += 4;
        src += 4;
        count -= 4;
    }
    if (count & 2) {
        *(tamp_ua_u16 *)dst = *(const tamp_ua_u16 *)src;
        dst += 2;
        src += 2;
    }
    if (count & 1) *dst = *src;
}

/**
 * @brief tamp_window_copy with a fast path for the common non-overlapping case.
 *
 * Copies as one or two straight word-copy segments (the destination may wrap;
 * the source never does - bounds pre-validated by caller) with a single
 * window_pos update. Any remaining linear overlap has dst preceding src, which
 * tamp_copy_fwd handles. The rare propagating-overlap case delegates to the
 * generic tamp_window_copy so the circular-overlap semantics live in one place.
 */
static inline void tamp_decomp_window_copy(unsigned char *window, uint16_t *window_pos, uint16_t window_offset,
                                           uint8_t match_size, uint16_t window_mask) {
    const uint16_t src_to_dst = (*window_pos - window_offset) & window_mask;
    if (TAMP_LIKELY(src_to_dst >= match_size || src_to_dst == 0)) {
        const uint32_t wp = *window_pos;
        const uint32_t window_size = (uint32_t)window_mask + 1;
        uint32_t l1 = window_size - wp;
        if (l1 > match_size) l1 = match_size;
        tamp_copy_fwd(window + wp, window + window_offset, l1);
        tamp_copy_fwd(window, window + window_offset + l1, match_size - l1);
        *window_pos = (wp + match_size) & window_mask;
    } else {
        tamp_window_copy(window, window_pos, window_offset, match_size, window_mask);
    }
}

/* Copy count bytes from src to the output cursor and advance it. */
#define TAMP_COPY_TO_OUTPUT(out, src, count)  \
    do {                                      \
        tamp_copy_fwd((out), (src), (count)); \
        (out) += (count);                     \
    } while (0)

#define TAMP_WINDOW_COPY(window, window_pos, window_offset, match_size, window_mask) \
    tamp_decomp_window_copy((window), (window_pos), (window_offset), (match_size), (window_mask))

#ifdef __cplusplus
}
#endif

#endif /* TAMP_ESP32_TAMP_COPY_H */
