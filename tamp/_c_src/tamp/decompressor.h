#ifndef TAMP_DECOMPRESSOR_H
#define TAMP_DECOMPRESSOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

typedef struct {
    unsigned char *window;
    uint32_t bit_buffer;
    uint32_t bit_buffer_pos:5;
    uint32_t min_pattern_size:2;
} TampDecompressor;

/**
 * @brief Read tamp header and populate configuration.
 *
 * Don't invoke if setting conf to NULL in tamp_decompressor_init.
 *
 * @param[out] conf Configuration read from header
 * @param[in] data Tamp compressed data stream.
 */
tamp_res tamp_decompressor_read_header(TampConf *conf, const unsigned char *input, size_t input_size, size_t *input_consumed_size);

/**
 * @brief Initialize decompressor object.
 *
 *
 *
 * @param[in,out] TampDecompressor object to perform decompression with.
 * @param[in] conf Compressor configuration. Set to NULL to perform an implicit header read.
 * @param[in] window Pre-allocated window buffer. Size must agree with conf->window.
 *                   If conf.use_custom_dictionary is true, then the window must be
 *                   externally initialized.
 */
tamp_res tamp_decompressor_init(TampDecompressor decompressor, const TampConf *conf, unsigned char *window);

/**
 * @brief
 *
 * @param[in,out] TampDecompressor object to perform decompression with.
 * @param[out] output Pointer to a pre-allocated buffer to hold the output decompressed data.
 * @param[in] output_size Size of the pre-allocated buffer. Will decompress up-to this many bytes.
 * @param[out] output_written_size Number of bytes written to output. May be NULL.
 * @param[in] input Pointer to the compressed input data.
 * @param[in] input_size Number of bytes in input data.
 * @param[out] input_consumed_size Number of bytes of input data consumed. May be NULL.
 *
 * @return Tamp Status Code.

 */
tamp_res tamp_decompressor_decompress(
        TampDecompressor *decompressor,
        unsigned char *output,
        size_t output_size,
        size_t *output_written_size,
        const unsigned char *input,
        size_t input_size,
        size_t *input_consumed_size
        );

#ifdef __cplusplus
}
#endif

#endif
