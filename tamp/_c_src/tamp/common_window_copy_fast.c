/**
 * @file common_window_copy_fast.c
 * @brief tamp_window_copy with a no-wrap fast path (default).
 *
 * NOTE: This file is #include'd by common.c, not compiled separately.
 * The common case (destination run does not wrap) copies without per-byte
 * masking; see the dispatch in common.c for why Cortex-M0/M0+ opts out.
 */

void tamp_window_copy(unsigned char *window, uint16_t *window_pos, uint16_t window_offset, uint8_t match_size,
                      uint16_t window_mask) {
    uint16_t pos = *window_pos;
    /* Calculate distance from source to destination in circular buffer.
     * src_to_dst = (dst - src) & mask gives the forward distance. */
    const uint16_t src_to_dst = (pos - window_offset) & window_mask;

    /* Critical overlap case: destination is AHEAD of source and they overlap.
     * When dst > src by less than match_size, a forward copy corrupts data because
     * we write to positions before reading from them.
     *
     * Example: src=100, dst=105, match_size=8
     *   - Forward copy at i=5 would read window[105], but we already overwrote it at i=0!
     *   - Must copy in REVERSE order (end to start) to read source bytes before overwriting.
     */
    if (TAMP_UNLIKELY(src_to_dst < match_size && src_to_dst > 0)) {
        /* Copy in reverse order: start from last byte, work backwards to first byte.
         * This ensures we read all overlapping source bytes before they're overwritten.
         * Destination wraps via mask; source doesn't need wrapping (pre-validated bounds). */
        for (uint8_t i = match_size; i-- > 0;) {
            window[(pos + i) & window_mask] = window[window_offset + i];
        }
        pos = (pos + match_size) & window_mask;
    } else if (TAMP_LIKELY((uint32_t)pos + match_size <= (uint32_t)window_mask + 1)) {
        /* Common case: destination run doesn't wrap (source is pre-validated
         * and never wraps), so no per-byte masking is needed. */
        unsigned char *dst = window + pos;
        const unsigned char *src = window + window_offset;
        for (uint8_t i = 0; i < match_size; i++) {
            dst[i] = src[i];
        }
        pos = (pos + match_size) & window_mask;
    } else {
        for (uint8_t i = 0; i < match_size; i++) {
            window[pos] = window[window_offset + i];
            pos = (pos + 1) & window_mask;
        }
    }
    *window_pos = pos;
}
