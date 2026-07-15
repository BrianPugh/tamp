/**
 * @file common_window_copy_compact.c
 * @brief Compact tamp_window_copy (per-byte masked circular copy).
 *
 * NOTE: This file is #include'd by common.c, not compiled separately.
 * Selected for size/register-constrained cores (Cortex-M0/M0+); see the
 * dispatch in common.c.
 */

void tamp_window_copy(unsigned char *window, uint16_t *window_pos, uint16_t window_offset, uint8_t match_size,
                      uint16_t window_mask) {
    /* Calculate distance from source to destination in circular buffer.
     * src_to_dst = (dst - src) & mask gives the forward distance. */
    const uint16_t src_to_dst = (*window_pos - window_offset) & window_mask;

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
            window[(*window_pos + i) & window_mask] = window[window_offset + i];
        }
        *window_pos = (*window_pos + match_size) & window_mask;
    } else {
        for (uint8_t i = 0; i < match_size; i++) {
            window[*window_pos] = window[window_offset + i];
            *window_pos = (*window_pos + 1) & window_mask;
        }
    }
}
