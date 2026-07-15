/**
 * @file compressor_find_match_embedded.c
 * @brief Embedded/default find_best_match implementation.
 *
 * NOTE: This file is #include'd by compressor.c, not compiled separately.
 *
 * Single-byte-first comparison with a word-at-a-time skip loop; the portable
 * fallback, safe for all architectures (no unaligned loads, no endianness
 * assumptions).
 */

/**
 * @brief Find the best match for the current input buffer.
 *
 * Embedded/32-bit implementation: uses single-byte-first comparison (faster on simple cores).
 *
 * @param[in,out] compressor TampCompressor object to perform search on.
 * @param[out] match_index  If match_size is 0, this value is undefined.
 * @param[out] match_size Size of best found match.
 */
static TAMP_NOINLINE void find_best_match(TampCompressor *compressor, uint16_t *match_index, uint8_t *match_size) {
    *match_size = 0;

    if (TAMP_UNLIKELY(compressor->input_size < compressor->min_pattern_size)) return;

    const uint8_t first_byte = read_input(0);
    const uint8_t second_byte = read_input(1);
    const uint32_t window_size_minus_1 = WINDOW_SIZE - 1;
    const uint8_t max_pattern_size = MIN(compressor->input_size, MAX_PATTERN_SIZE);
    const unsigned char *window = compressor->window;

#if defined(__GNUC__)
    // Word-at-a-time skip over bytes that can't start a match: ~3 cycles/byte
    // instead of ~9 on Cortex-M0+, and the scan dominates compression time.
    const uint32_t first_broadcast = first_byte * 0x01010101u;
#endif

    for (uint32_t window_index = 0; window_index < window_size_minus_1; window_index++) {
        if (TAMP_LIKELY(window[window_index] != first_byte)) {
#if defined(__GNUC__)
            uint32_t wi = window_index + 1;
            if (TAMP_LIKELY(((uintptr_t)(window + wi) & 3) == 0)) {
                // Aligned: skip 4 bytes per iteration until a word may hold
                // first_byte (zero-byte detect on the XOR; false positives
                // are fine, the byte loop re-checks).
                typedef uint32_t __attribute__((may_alias)) tamp_word_alias;
                while (wi + 4 <= window_size_minus_1) {
                    uint32_t word = *(const tamp_word_alias *)(window + wi) ^ first_broadcast;
                    if ((word - 0x01010101u) & ~word & 0x80808080u) break;
                    wi += 4;
                }
            }
            window_index = wi - 1;  // loop increment advances to wi
#endif
            continue;
        }
        if (TAMP_LIKELY(window[window_index + 1] != second_byte)) {
            continue;
        }

        // Found 2-byte match, now extend the match
        uint8_t match_len = 2;

        // Extend match byte by byte with optimized bounds checking
        for (uint8_t i = 2; i < max_pattern_size; i++) {
            if (TAMP_UNLIKELY((window_index + i) > window_size_minus_1)) break;

            if (TAMP_LIKELY(window[window_index + i] != read_input(i))) break;
            match_len = i + 1;
        }

        // Update best match if this is better
        if (TAMP_UNLIKELY(match_len > *match_size)) {
            *match_size = match_len;
            *match_index = window_index;
            // Early termination if we found the maximum possible match
            if (TAMP_UNLIKELY(*match_size == max_pattern_size)) return;
        }
    }
}
