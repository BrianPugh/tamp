#ifndef TAMP_DECOMPRESSOR_H
#define TAMP_DECOMPRESSOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

/* Externally, do not directly edit ANY of these attributes.
 * Fields are ordered by access frequency for cache efficiency.
 */
typedef struct {
    /* HOT: accessed every iteration of the decompression loop.
     * Full-width types avoid bitfield access overhead. */
    unsigned char *window;   // Pointer to window buffer
    uint32_t bit_buffer;     // Bit buffer for reading compressed data (32 bits)
    uint16_t window_pos;     // Current position in window (15 bits)
    uint8_t bit_buffer_pos;  // Bits currently in bit_buffer (6 bits)

    /* WARM: read once at start of decompress, cached in locals */
    uint8_t conf_window : 4;       // Window bits from config
    uint8_t conf_literal : 4;      // Literal bits from config
    uint8_t min_pattern_size : 2;  // Minimum pattern size, 2 or 3

    /* COLD: rarely accessed (init or edge cases).
     * Bitfields save space; add new cold fields here. */
    uint8_t skip_bytes : 4;       // For output-buffer-limited resumption
    uint8_t window_bits_max : 4;  // Max window bits buffer can hold
    uint8_t configured : 1;       // Whether config has been set
} TampDecompressor;

/**
 * @brief Read tamp header and populate configuration.
 *
 * Don't invoke if setting conf to NULL in tamp_decompressor_init.
 *
 * @param[out] conf Configuration read from header
 * @param[in] data Tamp compressed data stream.
 */
tamp_res tamp_decompressor_read_header(TampConf *conf, const unsigned char *input, size_t input_size,
                                       size_t *input_consumed_size);

/**
 * @brief Initialize decompressor object.
 *
 * @param[in,out] decompressor TampDecompressor object to perform decompression with.
 * @param[in] conf Decompressor configuration. Set to NULL to perform an implicit header read.
 * @param[in] window Pre-allocated window buffer.
 * @param[in] window_bits Number of window bits the buffer can accommodate (8-15).
 *                        Buffer must be at least (1 << window_bits) bytes.
 *                        When conf is NULL (implicit header read), the header's window size
 *                        is validated against this value.
 *
 * @return TAMP_OK on success, TAMP_INVALID_CONF if window_bits is invalid or too small.
 */
tamp_res tamp_decompressor_init(TampDecompressor *decompressor, const TampConf *conf, unsigned char *window,
                                uint8_t window_bits);

/**
 * Callback-variant of tamp_compressor_decompress.
 *
 * @param[in] callback User-provided function to be called every decompression-cycle.
 * @param[in,out] user_data Passed along to callback.
 */
tamp_res tamp_decompressor_decompress_cb(TampDecompressor *decompressor, unsigned char *output, size_t output_size,
                                         size_t *output_written_size, const unsigned char *input, size_t input_size,
                                         size_t *input_consumed_size, tamp_callback_t callback, void *user_data);

/**
 * @brief Decompress an input stream of data.
 *
 * Input data is **not** guaranteed to be consumed.  Imagine if a 6-byte sequence has been encoded,
 * and tamp_decompressor_decompress is called multiple times with a 2-byte output buffer:
 *
 *     1.  On the 1st call, a few input bytes may be consumed, filling the internal input buffer.
 *         The first 2 bytes of the 6-byte output sequence are returned.
 *         The internal input buffer remains full.
 *     2.  On the 2nd call, no input bytes are consumed since the internal input buffer is still
 * full. The {3, 4} bytes of the 6-byte output sequence are returned. The internal input buffer
 * remains full.
 *     3.  On the 3rd call, no input bytes are consumed since the internal input buffer is still
 * full. The {5, 6} bytes of the 6-byte output sequence are returned. The input buffer is no longer
 * full since this sequence has now been fully decoded.
 *     4.  On the 4th call, more input bytes are consumed, potentially filling the internal input
 * buffer. It is not strictly necessary for the internal input buffer to be full to further decode
 * the output. There simply has to be enough to decode a token/literal. If there is not enough bits
 * in the internal input buffer, then TAMP_INPUT_EXHAUSTED will be returned.
 *
 * @param[in,out] TampDecompressor object to perform decompression with.
 * @param[out] output Pointer to a pre-allocated buffer to hold the output decompressed data.
 * @param[in] output_size Size of the pre-allocated buffer. Will decompress up-to this many bytes.
 * @param[out] output_written_size Number of bytes written to output. May be NULL.
 * @param[in] input Pointer to the compressed input data.
 * @param[in] input_size Number of bytes in input data.
 * @param[out] input_consumed_size Number of bytes of input data consumed. May be NULL.
 *
 * @return Tamp Status Code. In cases of success, will return TAMP_INPUT_EXHAUSTED or
 * TAMP_OUTPUT_FULL, in lieu of TAMP_OK.
 */
TAMP_ALWAYS_INLINE tamp_res tamp_decompressor_decompress(TampDecompressor *decompressor, unsigned char *output,
                                                         size_t output_size, size_t *output_written_size,
                                                         const unsigned char *input, size_t input_size,
                                                         size_t *input_consumed_size) {
    return tamp_decompressor_decompress_cb(decompressor, output, output_size, output_written_size, input, input_size,
                                           input_consumed_size, NULL, NULL);
}

#ifdef __cplusplus
}
#endif

#endif
