#ifndef TAMP_DECOMPRESSOR_FUZZ_CASE_H
#define TAMP_DECOMPRESSOR_FUZZ_CASE_H

/* Single source of truth for the decompressor fuzz-corpus byte contract.
 *
 * Shared by the libFuzzer harness (fuzz/fuzz_decompressor.c) and the QEMU
 * adversarial-replay firmware (tools/qemu-profiler/firmware/replay_main.c) so
 * the corpus encoding (config byte, chunk byte, chunked feed/stall loop) cannot
 * drift between the two. Pure computation only: static-const table plus one
 * static-inline driver. Callers own their buffers and any post-run checks
 * (libFuzzer inspects results for the sanitizers; the firmware checks canaries
 * and return codes).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tamp/decompressor.h"

/* Chunk sizes chosen around token boundaries: single bytes stress per-call
 * resume, 17/31/33 straddle max classic match and the fast-loop 32-byte
 * output precondition, 241 is the max RLE run, 4096 approximates "large". */
static const uint16_t TAMP_FUZZ_CHUNK_SIZES[16] = {1, 2, 3, 4, 5, 7, 8, 16, 17, 31, 32, 33, 64, 241, 256, 4096};

/* Decode+drive one corpus entry against a caller-provided window (>= 1<<15
 * bytes) and output buffer (output_size bytes). Behavior is bit-identical to
 * the original libFuzzer LLVMFuzzerTestOneInput; the clamps below are inert
 * given the 3-bit/2-bit fields but are kept so the contract stays exact.
 * Returns 0 (callers own their own pass/fail accounting). */
static inline int tamp_fuzz_case_run(const uint8_t *data, size_t size, unsigned char *window, unsigned char *output,
                                     size_t output_size) {
    if (size < 2) return 0;

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

    size_t out_chunk = TAMP_FUZZ_CHUNK_SIZES[chunk_byte & 0x0F];
    size_t in_chunk = TAMP_FUZZ_CHUNK_SIZES[(chunk_byte >> 4) & 0x0F];
    if (out_chunk > output_size) out_chunk = output_size;

    TampDecompressor decompressor;
    tamp_res res;

    if (custom_dictionary) {
        // Custom-dictionary streams skip tamp_initialize_dictionary; the
        // window contents are attacker-visible but must never be read or
        // written out of bounds.
        memset(window, 0xA5, (size_t)1 << 15);
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

#endif /* TAMP_DECOMPRESSOR_FUZZ_CASE_H */
