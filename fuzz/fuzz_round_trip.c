/**
 * Fuzz harness: compress arbitrary input, then decompress and verify round-trip.
 *
 * Catches correctness bugs where compress->decompress doesn't reproduce
 * the original data. Tests across varying window/literal configurations.
 */
#include <assert.h>
#include <string.h>

#include "tamp/common.h"
#include "tamp/compressor.h"
#include "tamp/decompressor.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return 0;

    // Use first byte to select configuration
    uint8_t config_byte = data[0];
    data++;
    size--;

    if (size == 0) return 0;

    uint8_t window_bits = 8 + (config_byte & 0x07);  // 8-15
    if (window_bits > 15) window_bits = 15;
    uint8_t literal_bits = 5 + ((config_byte >> 3) & 0x03);  // 5-8
    if (literal_bits > 8) literal_bits = 8;
    bool extended = (config_byte >> 5) & 1;

    // Compress
    unsigned char comp_window[1 << 15];
    TampCompressor compressor;
    TampConf conf = {
        .window = window_bits,
        .literal = literal_bits,
        .use_custom_dictionary = 0,
        .extended = extended,
    };

    tamp_res res = tamp_compressor_init(&compressor, &conf, comp_window);
    if (res != TAMP_OK) return 0;

    // Worst case: compressed data could be larger than input
    size_t compressed_buf_size = size * 2 + 256;
    if (compressed_buf_size > 1024 * 1024) compressed_buf_size = 1024 * 1024;
    unsigned char *compressed = (unsigned char *)malloc(compressed_buf_size);
    if (!compressed) return 0;

    size_t compressed_size = 0;
    size_t input_consumed = 0;
    res = tamp_compressor_compress_and_flush(&compressor, compressed, compressed_buf_size, &compressed_size, data, size,
                                             &input_consumed, false);

    if (res != TAMP_OK || input_consumed != size) {
        free(compressed);
        return 0;
    }

    // Decompress — pass NULL conf so the decompressor reads the header
    // that the compressor wrote into the stream.
    unsigned char decomp_window[1 << 15];
    TampDecompressor decompressor;
    res = tamp_decompressor_init(&decompressor, NULL, decomp_window, 15);
    if (res != TAMP_OK) {
        free(compressed);
        return 0;
    }

    unsigned char *decompressed = (unsigned char *)malloc(size + 256);
    if (!decompressed) {
        free(compressed);
        return 0;
    }

    size_t decompressed_size = 0;
    size_t comp_consumed = 0;
    res = tamp_decompressor_decompress(&decompressor, decompressed, size + 256, &decompressed_size, compressed,
                                       compressed_size, &comp_consumed);

    // Verify round-trip
    assert(decompressed_size == size);
    assert(memcmp(data, decompressed, size) == 0);

    free(compressed);
    free(decompressed);
    return 0;
}
