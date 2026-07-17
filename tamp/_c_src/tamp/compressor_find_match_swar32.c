/**
 * @file compressor_find_match_swar32.c
 * @brief 32-bit SWAR find_best_match for 32-bit cores with cheap unaligned
 * loads (e.g. Cortex-M7/M33, RISC-V with fast misaligned access).
 *
 * NOTE: This file is #include'd by compressor.c, not compiled separately.
 *
 * Same algorithm as compressor_find_match_desktop.c, narrowed to 32-bit
 * words: two overlapping 32-bit loads check 6 window positions per iteration
 * for a 2-byte match seed, and match extension compares 4 bytes at a time.
 * Produces byte-identical output to the embedded and desktop implementations
 * (ascending scan, strictly-longer replacement).
 *
 * Requirements:
 *   - Little-endian byte order
 *   - Efficient unaligned 32-bit loads (memcpy must lower to a plain load)
 *   - __builtin_ctz (GCC/Clang)
 */

/* Fail loudly on toolchains without __builtin_ctz (e.g. MSVC). Selection is a
 * build-system decision, so a wrong opt-in must be a clear compile error, not
 * an undefined-symbol surprise. This finder targets embedded GCC/Clang cores;
 * MSVC targets should use the desktop matcher (64-bit) or the portable scan. */
#if !defined(__GNUC__) && !defined(__clang__)
#error "TAMP_USE_SWAR32_MATCH requires __builtin_ctz (GCC/Clang)"
#endif

#include <string.h>  // for memcpy (portable unaligned loads)

#define tamp_ctz(v) __builtin_ctz(v)

/* Detect zero bytes in a 32-bit word (classic bit manipulation trick).
 * Can have false positives when a zero byte is followed by 0x01-0x7F due to
 * borrow propagation; callers re-verify every hit. */
static inline uint32_t tamp_has_zero_byte32(uint32_t v) { return (v - 0x01010101u) & ~v & 0x80808080u; }

/**
 * @brief Extend a verified 2-byte candidate match at idx (32-bit variant).
 *
 * Pure computation (callers update the best match): uses the pre-computed
 * input_word_ext and input_bytes to avoid repeated read_input() calls.
 *
 * @return The match length at idx.
 */
static inline uint8_t tamp_match_extend(const unsigned char *window, uint16_t window_size_minus_1,
                                        uint8_t max_pattern_size, const uint8_t *input_bytes, uint32_t input_word_ext,
                                        uint16_t idx) {
    uint8_t match_len = 2;

    /* Try 4-byte comparison for bytes 2-5 using pre-computed input_word_ext */
    if (max_pattern_size >= 6 && (idx + 5) <= window_size_minus_1) {
        uint32_t window_word;
        memcpy(&window_word, window + idx + 2, sizeof(uint32_t));
        uint32_t diff = window_word ^ input_word_ext;
        if (diff != 0) return 2 + (tamp_ctz(diff) >> 3);
        match_len = 6;
    }

    /* Byte-by-byte for the remainder using pre-loaded input_bytes */
    for (uint8_t i = match_len; i < max_pattern_size; i++) {
        if (TAMP_UNLIKELY((idx + i) > window_size_minus_1)) break;
        if (TAMP_LIKELY(window[idx + i] != input_bytes[i])) break;
        match_len = i + 1;
    }
    return match_len;
}

/* Update the best match with a freshly extended candidate; evaluates to
 * true when the maximum possible match was found and scanning can stop. */
#define TAMP_MATCH_UPDATE(idx, len)        \
    (TAMP_UNLIKELY((len) > *match_size) && \
     (*match_size = (len), *match_index = (idx), TAMP_UNLIKELY(*match_size == max_pattern_size)))

/**
 * @brief Find the best match for the current input buffer.
 *
 * 32-bit SWAR implementation: uses bit manipulation to detect multiple 2-byte
 * matches simultaneously within 32-bit words.
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

    // Broadcast patterns for detecting first_byte and second_byte in parallel
    const uint32_t first_pattern = 0x01010101u * input_bytes[0];
    const uint32_t second_pattern = 0x01010101u * input_bytes[1];

    // Pre-compute 32-bit input word for extension (bytes 2-5)
    const uint32_t input_word_ext = (uint32_t)input_bytes[2] | ((uint32_t)input_bytes[3] << 8) |
                                    ((uint32_t)input_bytes[4] << 16) | ((uint32_t)input_bytes[5] << 24);

    uint16_t window_index = 0;

    // Main loop: check 6 positions per iteration using two 32-bit loads
    for (; window_index + 7 < window_size_minus_1; window_index += 6) {
        uint32_t word1, word2;
        memcpy(&word1, window + window_index, sizeof(uint32_t));
        memcpy(&word2, window + window_index + 3, sizeof(uint32_t));

        // Find 2-byte matches in word1 (positions 0-2)
        // XOR with broadcast patterns to find matching bytes, then detect zeros
        uint32_t first_zeros1 = tamp_has_zero_byte32(word1 ^ first_pattern);
        uint32_t second_zeros1 = tamp_has_zero_byte32(word1 ^ second_pattern);
        // A 2-byte match at position i requires first_byte at i AND second_byte at i+1
        uint32_t matches1 = first_zeros1 & (second_zeros1 >> 8);

        // Process all matches found in word1
        // Note: tamp_has_zero_byte32 can have false positives, so verify before extending
        while (matches1) {
            int byte_pos = tamp_ctz(matches1) >> 3;
            uint16_t candidate;
            memcpy(&candidate, window + window_index + byte_pos, sizeof(uint16_t));
            if (TAMP_UNLIKELY(candidate == first_second)) {
                uint8_t len = tamp_match_extend(window, window_size_minus_1, max_pattern_size, input_bytes,
                                                input_word_ext, window_index + byte_pos);
                if (TAMP_MATCH_UPDATE(window_index + byte_pos, len)) return;
            }
            matches1 &= matches1 - 1;  // Clear lowest set bit
        }

        // Find 2-byte matches in word2 (positions 3-5)
        uint32_t first_zeros2 = tamp_has_zero_byte32(word2 ^ first_pattern);
        uint32_t second_zeros2 = tamp_has_zero_byte32(word2 ^ second_pattern);
        uint32_t matches2 = first_zeros2 & (second_zeros2 >> 8);

        while (matches2) {
            int byte_pos = tamp_ctz(matches2) >> 3;
            uint16_t candidate;
            memcpy(&candidate, window + window_index + 3 + byte_pos, sizeof(uint16_t));
            if (TAMP_UNLIKELY(candidate == first_second)) {
                uint8_t len = tamp_match_extend(window, window_size_minus_1, max_pattern_size, input_bytes,
                                                input_word_ext, window_index + 3 + byte_pos);
                if (TAMP_MATCH_UPDATE(window_index + 3 + byte_pos, len)) return;
            }
            matches2 &= matches2 - 1;
        }
    }

    // Handle remaining positions
    for (; window_index < window_size_minus_1; window_index++) {
        uint16_t candidate;
        memcpy(&candidate, window + window_index, sizeof(uint16_t));
        if (TAMP_LIKELY(candidate != first_second)) {
            continue;
        }
        uint8_t len =
            tamp_match_extend(window, window_size_minus_1, max_pattern_size, input_bytes, input_word_ext, window_index);
        if (TAMP_MATCH_UPDATE(window_index, len)) return;
    }
}

#undef TAMP_MATCH_UPDATE
#undef tamp_ctz
