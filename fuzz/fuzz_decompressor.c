/**
 * Fuzz harness: feed arbitrary bytes to the decompressor.
 *
 * This is the highest-priority target since decompressors typically
 * handle untrusted input. We test across all valid window/literal
 * configurations, with and without extended format, and across
 * fuzz-chosen input/output chunk sizes so the suspend/resume state
 * machines (mid-token INPUT_EXHAUSTED, output-full skip_bytes /
 * token_state resume, header-byte stash) run against malicious data,
 * not just the happy full-buffer path.
 *
 * Build with the portable defaults AND with the ARMV7EM-profile flags
 * (TAMP_FAST_DECODE_LOOP=1 TAMP_WINDOW_FROM_OUTPUT=1 TAMP_FAST_WINDOW_COPY=1
 *  TAMP_FAST_BIT_REFILL=1 TAMP_FAST_OUTPUT_COPY=1) - the fast decode loop and
 * the inline window-update variants are compiled out otherwise and would
 * never be fuzzed.
 */
#include <string.h>

#include "tamp/common.h"
#include "tamp/decompressor.h"

/* Chunk sizes chosen around token boundaries: single bytes stress per-call
 * resume, 17/31/33 straddle max classic match and the fast-loop 32-byte
 * output precondition, 241 is the max RLE run, 4096 approximates "large". */
static const size_t CHUNK_SIZES[16] = {1, 2, 3, 4, 5, 7, 8, 16, 17, 31, 32, 33, 64, 241, 256, 4096};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) return 0;

    // Use first two bytes to select configuration
    uint8_t config_byte = data[0];
    uint8_t chunk_byte = data[1];
    data += 2;
    size -= 2;

    uint8_t window_bits = 8 + (config_byte & 0x07);  // 8-15
    if (window_bits > 15) window_bits = 15;
    uint8_t literal_bits = 5 + ((config_byte >> 3) & 0x03);  // 5-8
    if (literal_bits > 8) literal_bits = 8;
    bool extended = (config_byte >> 5) & 1;
    bool use_header = (config_byte >> 6) & 1;
    bool custom_dictionary = (config_byte >> 7) & 1;

    size_t out_chunk = CHUNK_SIZES[chunk_byte & 0x0F];
    size_t in_chunk = CHUNK_SIZES[(chunk_byte >> 4) & 0x0F];

    unsigned char window[1 << 15];  // Max window size
    TampDecompressor decompressor;
    tamp_res res;

    if (custom_dictionary) {
        // Custom-dictionary streams skip tamp_initialize_dictionary; the
        // window contents are attacker-visible but must never be read or
        // written out of bounds.
        memset(window, 0xA5, sizeof(window));
    }

    if (use_header) {
        // Let the decompressor read the header from the stream
        res = tamp_decompressor_init(&decompressor, NULL, window, 15);
    } else {
        TampConf conf = {
            .window = window_bits,
            .literal = literal_bits,
            .use_custom_dictionary = custom_dictionary,
            .extended = extended,
        };
        res = tamp_decompressor_init(&decompressor, &conf, window, window_bits);
    }
    if (res != TAMP_OK) return 0;

    unsigned char output[4096];
    if (out_chunk > sizeof(output)) out_chunk = sizeof(output);

    const unsigned char *remaining = data;
    size_t remaining_size = size;
    int stalled = 0;

    while (remaining_size > 0) {
        size_t feed = remaining_size < in_chunk ? remaining_size : in_chunk;
        size_t consumed = 0;
        size_t written = 0;
        res = tamp_decompressor_decompress(&decompressor, output, out_chunk, &written, remaining, feed, &consumed);
        remaining += consumed;
        remaining_size -= consumed;

        if (res < 0 && res != TAMP_INPUT_EXHAUSTED) break;
        // OUTPUT_FULL with input left keeps looping (drains via resume paths);
        // bail once no progress is made on either side twice in a row.
        if (consumed == 0 && written == 0) {
            if (++stalled >= 2) break;
        } else {
            stalled = 0;
        }
        if (res == TAMP_INPUT_EXHAUSTED && consumed == 0 && feed == remaining_size) break;
    }

    return 0;
}
