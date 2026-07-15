/**
 * @file decompressor_refill_compact.c
 * @brief Compact refill_bit_buffer (operates through the pointers).
 *
 * NOTE: This file is #include'd by decompressor.c, not compiled separately.
 * Selected for Cortex-M0/M0+; see the dispatch in decompressor.c.
 */

static inline void refill_bit_buffer(TampDecompressor* d, const unsigned char** input, const unsigned char* input_end,
                                     size_t* input_consumed_size) {
    while (*input != input_end && d->bit_buffer_pos <= 24) {
        d->bit_buffer_pos += 8;
        d->bit_buffer |= (uint32_t) * (*input) << (32 - d->bit_buffer_pos);
        (*input)++;
        (*input_consumed_size)++;
    }
}
