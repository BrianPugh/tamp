/**
 * Fuzz harness: feed arbitrary bytes to the decompressor.
 *
 * This is the highest-priority target since decompressors typically
 * handle untrusted input. We test across all valid window/literal
 * configurations, both with and without extended format.
 */
#include "tamp/common.h"
#include "tamp/decompressor.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return 0;

    // Use first byte to select configuration
    uint8_t config_byte = data[0];
    data++;
    size--;

    uint8_t window_bits = 8 + (config_byte & 0x07);  // 8-15
    if (window_bits > 15) window_bits = 15;
    uint8_t literal_bits = 5 + ((config_byte >> 3) & 0x03);  // 5-8
    if (literal_bits > 8) literal_bits = 8;
    bool extended = (config_byte >> 5) & 1;
    bool use_header = (config_byte >> 6) & 1;

    unsigned char window[1 << 15];  // Max window size
    TampDecompressor decompressor;
    tamp_res res;

    if (use_header) {
        // Let the decompressor read the header from the stream
        res = tamp_decompressor_init(&decompressor, NULL, window, 15);
    } else {
        TampConf conf = {
            .window = window_bits,
            .literal = literal_bits,
            .use_custom_dictionary = 0,
            .extended = extended,
        };
        res = tamp_decompressor_init(&decompressor, &conf, window, window_bits);
    }
    if (res != TAMP_OK) return 0;

    unsigned char output[4096];
    size_t input_consumed_size = 0;
    size_t output_written_size = 0;

    // Decompress in chunks to exercise the output-full resume path
    const unsigned char *remaining = data;
    size_t remaining_size = size;

    while (remaining_size > 0) {
        size_t consumed = 0;
        size_t written = 0;
        res = tamp_decompressor_decompress(&decompressor, output, sizeof(output), &written, remaining, remaining_size,
                                           &consumed);
        remaining += consumed;
        remaining_size -= consumed;

        if (res < 0 && res != TAMP_INPUT_EXHAUSTED) break;
        if (res == TAMP_INPUT_EXHAUSTED && consumed == 0) break;
    }

    return 0;
}
