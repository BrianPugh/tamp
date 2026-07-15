/**
 * @file decompressor_refill_fast.c
 * @brief refill_bit_buffer on locals with a single writeback (default).
 *
 * NOTE: This file is #include'd by decompressor.c, not compiled separately.
 * The unsigned char loads may alias anything, so operating through the
 * pointers directly forces a reload/store per byte; locals avoid that
 * (measured +5% decompression speed on Cortex-M7).
 */

static inline void refill_bit_buffer(TampDecompressor* d, const unsigned char** input, const unsigned char* input_end,
                                     size_t* input_consumed_size) {
    const unsigned char* in = *input;
    uint32_t bit_buffer = d->bit_buffer;
    uint8_t bit_buffer_pos = d->bit_buffer_pos;
    size_t consumed = 0;
    while (in != input_end && bit_buffer_pos <= 24) {
        bit_buffer_pos += 8;
        bit_buffer |= (uint32_t)*in++ << (32 - bit_buffer_pos);
        consumed++;
    }
    d->bit_buffer = bit_buffer;
    d->bit_buffer_pos = bit_buffer_pos;
    *input = in;
    *input_consumed_size += consumed;
}
