/**
 * @file compressor_find_match_prefilter.c
 * @brief First-byte-prefilter find_best_match (default on ARMv7E-M; opt-in
 * elsewhere via TAMP_USE_PREFILTER_MATCH=1).
 *
 * NOTE: This file is #include'd by compressor.c, not compiled separately.
 *
 * Variant of compressor_find_match_desktop.c that scans for the first input
 * byte only: one 64-bit load and one zero-byte detect per 8 window positions,
 * verifying the 2-byte seed with a 16-bit load per candidate hit. Compared to
 * the desktop implementation this halves the per-word SWAR work (and the
 * live broadcast constants, easing register pressure on 32-bit cores); the
 * trade-off is more per-hit work when the first byte is frequent.
 * Produces byte-identical output to the other implementations (ascending
 * scan, strictly-longer replacement).
 *
 * Requirements:
 *   - Little-endian byte order
 *   - Efficient unaligned loads (memcpy must lower to plain loads)
 *   - __builtin_ctzll or _BitScanForward64
 */

#include <string.h>  // for memcpy (portable unaligned loads)

// MSVC compatibility for count trailing zeros
#if defined(_MSC_VER)
#include <intrin.h>
static inline int tamp_ctzll(uint64_t value) {
    unsigned long index;
    _BitScanForward64(&index, value);
    return (int)index;
}
#else
#define tamp_ctzll(v) __builtin_ctzll(v)
#endif

// Detect zero bytes in a 64-bit word (classic bit manipulation trick)
// Note: Can have false positives when a zero byte is followed by 0x01-0x7F due to borrow propagation
#define HAS_ZERO_BYTE(v) (((v)-0x0101010101010101ULL) & ~(v) & 0x8080808080808080ULL)

// Helper macro to extend a match and update best match
// Uses pre-computed input_word_ext and input_bytes to avoid repeated read_input() calls
#define EXTEND_MATCH(idx)                                                                        \
    do {                                                                                         \
        uint8_t match_len = 2;                                                                   \
        uint8_t i = 2;                                                                           \
        int extend_done = 0;                                                                     \
                                                                                                 \
        /* Best-length gate: a strictly longer match must also agree at offset  */               \
        /* *match_size, so a single byte compare rejects most candidates early. */               \
        if (*match_size && (TAMP_UNLIKELY((idx + *match_size) > window_size_minus_1) ||          \
                            TAMP_LIKELY(window[idx + *match_size] != input_bytes[*match_size]))) \
            break;                                                                               \
                                                                                                 \
        /* Try 8-byte comparison for bytes 2-9 using pre-computed input_word_ext */              \
        if (max_pattern_size >= 10 && (idx + 9) <= window_size_minus_1) {                        \
            uint64_t window_word;                                                                \
            memcpy(&window_word, window + idx + 2, sizeof(uint64_t));                            \
            uint64_t diff = window_word ^ input_word_ext;                                        \
            if (diff == 0) {                                                                     \
                match_len = 10;                                                                  \
                i = 10;                                                                          \
            } else {                                                                             \
                match_len = 2 + (tamp_ctzll(diff) >> 3);                                         \
                extend_done = 1;                                                                 \
            }                                                                                    \
        }                                                                                        \
                                                                                                 \
        /* Byte-by-byte for remainder using pre-loaded input_bytes */                            \
        if (!extend_done) {                                                                      \
            for (; i < max_pattern_size; i++) {                                                  \
                if (TAMP_UNLIKELY((idx + i) > window_size_minus_1)) break;                       \
                if (TAMP_LIKELY(window[idx + i] != input_bytes[i])) break;                       \
                match_len = i + 1;                                                               \
            }                                                                                    \
        }                                                                                        \
                                                                                                 \
        if (TAMP_UNLIKELY(match_len > *match_size)) {                                            \
            *match_size = match_len;                                                             \
            *match_index = idx;                                                                  \
            if (TAMP_UNLIKELY(*match_size == max_pattern_size)) return;                          \
        }                                                                                        \
    } while (0)

/**
 * @brief Find the best match for the current input buffer.
 *
 * First-byte prefilter: SWAR-detect candidate positions holding the first
 * input byte, then verify the 2-byte seed with a 16-bit load per hit.
 *
 * @param[in,out] compressor TampCompressor object to perform search on.
 * @param[out] match_index  If match_size is 0, this value is undefined.
 * @param[out] match_size Size of best found match.
 */
static inline void find_best_match(TampCompressor *compressor, uint16_t *match_index, uint8_t *match_size) {
    *match_size = 0;

    if (TAMP_UNLIKELY(compressor->input_size < compressor->min_pattern_size)) return;

    const uint16_t window_size_minus_1 = WINDOW_SIZE - 1;
    const uint8_t max_pattern_size = MIN(compressor->input_size, MAX_PATTERN_SIZE);
    const unsigned char *window = compressor->window;

    // Pre-load input bytes into linear array to avoid repeated modular arithmetic.
    // Zero-initialized: entries at max_pattern_size and beyond are read when
    // assembling input_word_ext below; their values never affect the result.
    uint8_t input_bytes[16] = {0};
    for (uint8_t i = 0; i < max_pattern_size && i < 16; i++) {
        input_bytes[i] = read_input(i);
    }

    // Little-endian format to match direct memory loads
    const uint16_t first_second = input_bytes[0] | (input_bytes[1] << 8);

    // Broadcast pattern for detecting first_byte in parallel
    const uint64_t first_pattern = 0x0101010101010101ULL * input_bytes[0];

    // Pre-compute 64-bit input word for extension (bytes 2-9)
    const uint64_t input_word_ext = (uint64_t)input_bytes[2] | ((uint64_t)input_bytes[3] << 8) |
                                    ((uint64_t)input_bytes[4] << 16) | ((uint64_t)input_bytes[5] << 24) |
                                    ((uint64_t)input_bytes[6] << 32) | ((uint64_t)input_bytes[7] << 40) |
                                    ((uint64_t)input_bytes[8] << 48) | ((uint64_t)input_bytes[9] << 56);

    uint16_t window_index = 0;

    // Main loop: scan 8 positions per iteration with one 64-bit load. A hit
    // at byte 7 verifies its pair byte via the unaligned 16-bit candidate
    // load, so no overlapping second word is needed.
    for (; window_index + 8 <= window_size_minus_1; window_index += 8) {
        uint64_t word;
        memcpy(&word, window + window_index, sizeof(uint64_t));

        // Positions whose byte may equal the first input byte.
        // Note: HAS_ZERO_BYTE can have false positives; the 16-bit candidate
        // check below re-verifies every hit.
        uint64_t hits = HAS_ZERO_BYTE(word ^ first_pattern);

        while (hits) {
            int byte_pos = tamp_ctzll(hits) >> 3;
            uint16_t candidate;
            memcpy(&candidate, window + window_index + byte_pos, sizeof(uint16_t));
            if (TAMP_UNLIKELY(candidate == first_second)) {
                EXTEND_MATCH(window_index + byte_pos);
            }
            hits &= hits - 1;  // Clear lowest set bit
        }
    }

    // Handle remaining positions
    for (; window_index < window_size_minus_1; window_index++) {
        uint16_t candidate;
        memcpy(&candidate, window + window_index, sizeof(uint16_t));
        if (TAMP_LIKELY(candidate != first_second)) {
            continue;
        }
        EXTEND_MATCH(window_index);
    }
}

#undef EXTEND_MATCH
#undef HAS_ZERO_BYTE
#if !defined(_MSC_VER)
#undef tamp_ctzll
#endif
