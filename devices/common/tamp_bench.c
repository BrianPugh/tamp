#include "tamp_bench.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "profiling.h"
#include "tamp/compressor.h"
#include "tamp/decompressor.h"

TampProfilingStats tamp_profiling_stats;

#define WINDOW_BITS 10
#define STRESS_BLOCK_SIZE 8192
#define STRESS_DEFAULT_ITERATIONS 10
#define STRESS_SEED 0x1234abcdu

/* Sized for the largest window used anywhere below (the stress block's w=12). */
static uint8_t window_buffer[1 << 12];
static uint8_t compressed_buffer[60000];
static uint8_t decompressed_buffer[100000];

static int failures;

#define REPORT(ok, ...)                     \
    do {                                    \
        printf((ok) ? "PASS: " : "FAIL: "); \
        printf(__VA_ARGS__);                \
        printf("\n");                       \
        if (!(ok)) failures++;              \
    } while (0)

static uint32_t xs_next(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static const char STRESS_ALPHABET[] = "abcdefghijklmnop";

/* Deterministic pseudo-random content generator.
 * Fills `out` when `verify` is NULL; otherwise regenerates the same stream and
 * returns the number of bytes that differ from `verify`. State advances
 * identically in both modes, so a snapshot of the pre-generation state can
 * reproduce the stream without keeping a second copy. */
static size_t stress_stream(int generator, size_t size, uint32_t *state, uint8_t *out, const uint8_t *verify) {
    size_t mismatches = 0;
    for (size_t i = 0; i < size; i++) {
        uint8_t b;
        switch (generator) {
            case 0:
                b = (uint8_t)xs_next(state);
                break;
            case 1:
                b = (uint8_t)STRESS_ALPHABET[xs_next(state) & 0x0f];
                break;
            default:
                if ((i % 50) == 0)
                    b = (uint8_t)xs_next(state);
                else
                    b = (uint8_t)(((i & 63) * 37 + 11) & 0xff);
                break;
        }
        if (verify) {
            if (verify[i] != b) mismatches++;
        } else {
            out[i] = b;
        }
    }
    return mismatches;
}

static void print_profiling_stats(void) {
    printf("  Profiling breakdown:\n");
    printf("    bit_buffer_fill: %llu us\n", (unsigned long long)tamp_profiling_stats.bit_buffer_fill_time_us);
    printf("    literal:         %llu us (%lu count)\n", (unsigned long long)tamp_profiling_stats.literal_time_us,
           (unsigned long)tamp_profiling_stats.literal_count);
    printf("    pattern_decode:  %llu us\n", (unsigned long long)tamp_profiling_stats.pattern_decode_time_us);
    printf("    pattern_output:  %llu us\n", (unsigned long long)tamp_profiling_stats.pattern_output_time_us);
    printf("    window_update:   %llu us (%lu patterns, %lu bytes avg)\n",
           (unsigned long long)tamp_profiling_stats.window_update_time_us,
           (unsigned long)tamp_profiling_stats.pattern_count,
           (unsigned long)(tamp_profiling_stats.pattern_count
                               ? tamp_profiling_stats.pattern_bytes_total / tamp_profiling_stats.pattern_count
                               : 0));
    printf("    overlap: %lu, non_overlap: %lu (%.1f%% overlap)\n", (unsigned long)tamp_profiling_stats.overlap_count,
           (unsigned long)tamp_profiling_stats.non_overlap_count,
           tamp_profiling_stats.pattern_count
               ? 100.0f * tamp_profiling_stats.overlap_count / tamp_profiling_stats.pattern_count
               : 0.0f);
}

int tamp_bench_run(const TampBenchData *data) {
    uint64_t start, end, elapsed;
    failures = 0;

    if (data->input_size > sizeof(decompressed_buffer)) {
        printf("FAIL: input larger than harness buffers (%u > %u)\n", (unsigned)data->input_size,
               (unsigned)sizeof(decompressed_buffer));
        printf("TAMP-DEVICE-RESULT: FAIL failures=1\n");
        return 1;
    }

    size_t compressed_size = 0;
    {
        /* Compress the input and compare against the embedded reference. */
        size_t output_written_size;
        tamp_res res;
        TampCompressor compressor;
        TampConf conf = {.window = WINDOW_BITS, .literal = 8, .use_custom_dictionary = false};

        res = tamp_compressor_init(&compressor, &conf, window_buffer);
        REPORT(res == TAMP_OK, "compressor_init enwik8 (res=%d)", res);

        start = tamp_bench_time_us();
        res = tamp_compressor_compress_and_flush(&compressor, compressed_buffer, sizeof(compressed_buffer),
                                                 &output_written_size, data->input, data->input_size, NULL, false);
        end = tamp_bench_time_us();
        elapsed = end - start;
        compressed_size = output_written_size;
        printf("BENCH compress_enwik8_us=%llu\n", (unsigned long long)elapsed);
        printf("INFO compressed_size=%u compress_bytes_per_sec=%llu\n", (unsigned)output_written_size,
               elapsed ? (unsigned long long)((uint64_t)data->input_size * 1000000 / elapsed) : 0);
        REPORT(res == TAMP_OK, "compress enwik8 (res=%d)", res);

        bool ok = (output_written_size == data->reference_size) &&
                  (memcmp(compressed_buffer, data->reference, data->reference_size) == 0);
        if (!ok) {
            if (output_written_size != data->reference_size)
                printf("  size mismatch: expected %u, got %u\n", (unsigned)data->reference_size,
                       (unsigned)output_written_size);
            size_t min_size = data->reference_size < output_written_size ? data->reference_size : output_written_size;
            for (size_t i = 0; i < min_size; i++) {
                if (compressed_buffer[i] != data->reference[i]) {
                    printf("  first difference at %u: expected 0x%02x, got 0x%02x\n", (unsigned)i, data->reference[i],
                           compressed_buffer[i]);
                    break;
                }
            }
        }
        REPORT(ok, "compress enwik8 matches reference");
    }

    {
        /* Decompress the embedded reference and compare against the original. */
        size_t output_written_size;
        tamp_res res;
        memset(decompressed_buffer, 0, sizeof(decompressed_buffer));
        TampDecompressor decompressor;
        res = tamp_decompressor_init(&decompressor, NULL, window_buffer, WINDOW_BITS);
        REPORT(res == TAMP_OK, "decompressor_init enwik8 (res=%d)", res);

        tamp_profiling_reset();
        start = tamp_bench_time_us();
        tamp_decompressor_decompress(&decompressor, decompressed_buffer, sizeof(decompressed_buffer),
                                     &output_written_size, data->reference, data->reference_size, NULL);
        end = tamp_bench_time_us();
        elapsed = end - start;
        printf("BENCH decompress_enwik8_us=%llu\n", (unsigned long long)elapsed);
        printf("INFO decompress_bytes_per_sec=%llu\n",
               elapsed ? (unsigned long long)((uint64_t)output_written_size * 1000000 / elapsed) : 0);

        bool ok = (output_written_size == data->input_size) &&
                  (memcmp(decompressed_buffer, data->input, data->input_size) == 0);
        REPORT(ok, "decompress reference matches original");
        print_profiling_stats();
    }

    {
        /* Round-trip: decompress our own compressed output. */
        size_t output_written_size;
        memset(decompressed_buffer, 0, sizeof(decompressed_buffer));
        TampDecompressor decompressor;
        tamp_decompressor_init(&decompressor, NULL, window_buffer, WINDOW_BITS);
        tamp_decompressor_decompress(&decompressor, decompressed_buffer, sizeof(decompressed_buffer),
                                     &output_written_size, compressed_buffer, compressed_size, NULL);

        bool ok = (output_written_size == data->input_size) &&
                  (memcmp(decompressed_buffer, data->input, data->input_size) == 0);
        if (!ok && output_written_size != data->input_size)
            printf("  size mismatch: expected %u, got %u\n", (unsigned)data->input_size, (unsigned)output_written_size);
        REPORT(ok, "round-trip enwik8");
    }

    {
        /* Repetitive synthetic data (long/extended matches dominate). */
        const size_t repetitive_size = sizeof(decompressed_buffer);
#define REPETITIVE_BYTE(i) ((i) % 257 == 0 ? '!' : (uint8_t)('A' + ((i) % 61)))
        for (size_t i = 0; i < repetitive_size; i++) decompressed_buffer[i] = REPETITIVE_BYTE(i);

        size_t output_written_size;
        tamp_res res;
        TampCompressor compressor;
        TampConf conf = {.window = WINDOW_BITS, .literal = 8, .use_custom_dictionary = false};
        tamp_compressor_init(&compressor, &conf, window_buffer);
        start = tamp_bench_time_us();
        res =
            tamp_compressor_compress_and_flush(&compressor, compressed_buffer, sizeof(compressed_buffer),
                                               &output_written_size, decompressed_buffer, repetitive_size, NULL, false);
        end = tamp_bench_time_us();
        printf("BENCH compress_repetitive_us=%llu\n", (unsigned long long)(end - start));
        REPORT(res == TAMP_OK, "compress repetitive (res=%d)", res);

        size_t comp_size = output_written_size;
        memset(decompressed_buffer, 0, sizeof(decompressed_buffer));
        TampDecompressor decompressor;
        tamp_decompressor_init(&decompressor, NULL, window_buffer, WINDOW_BITS);
        start = tamp_bench_time_us();
        tamp_decompressor_decompress(&decompressor, decompressed_buffer, sizeof(decompressed_buffer),
                                     &output_written_size, compressed_buffer, comp_size, NULL);
        end = tamp_bench_time_us();
        printf("BENCH decompress_repetitive_us=%llu\n", (unsigned long long)(end - start));

        bool ok = (output_written_size == repetitive_size);
        for (size_t i = 0; ok && i < repetitive_size; i++) {
            if (decompressed_buffer[i] != REPETITIVE_BYTE(i)) ok = false;
        }
        REPORT(ok, "round-trip repetitive");
#undef REPETITIVE_BYTE
    }

    if (data->vectors) {
        /* Vector replay: feed each embedded regression vector to a fresh
         * decompressor. Any tamp_res is acceptable; we only require no crash/hang. */
        const uint8_t *vp = data->vectors;
        uint32_t count = 0;
        if (data->vectors_size >= sizeof(count)) {
            memcpy(&count, vp, sizeof(count));
            vp += sizeof(count);
        }
        for (uint32_t i = 0; i < count; i++) {
            uint32_t len = 0;
            memcpy(&len, vp, sizeof(len));
            vp += sizeof(len);
            const uint8_t *vdata = vp;
            vp += len;

            TampDecompressor decompressor;
            tamp_decompressor_init(&decompressor, NULL, window_buffer, WINDOW_BITS);
            const uint8_t *input = vdata;
            size_t remaining = len;
            tamp_res res = TAMP_OK;
            for (;;) {
                size_t consumed = 0, written = 0;
                res = tamp_decompressor_decompress(&decompressor, decompressed_buffer, sizeof(decompressed_buffer),
                                                   &written, input, remaining, &consumed);
                input += consumed;
                remaining -= consumed;
                if (res != TAMP_OK && res != TAMP_INPUT_EXHAUSTED && res != TAMP_OUTPUT_FULL) break;
                if (remaining == 0 && res == TAMP_INPUT_EXHAUSTED) break;
                if (consumed == 0 && written == 0) break;
            }
            printf("PASS: vector %lu (res=%d)\n", (unsigned long)i, res);
        }
    }

    {
        /* Seeded PRNG stress: compress/decompress deterministic pseudo-random
         * content across several window sizes and content shapes. */
        int iterations = data->stress_iterations ? data->stress_iterations : STRESS_DEFAULT_ITERATIONS;
        uint32_t stress_state = STRESS_SEED;
        printf("INFO stress_seed=%u\n", (unsigned)STRESS_SEED);
        static const uint8_t window_bits_set[3] = {8, 10, 12};
        for (int wi = 0; wi < 3; wi++) {
            uint8_t wb = window_bits_set[wi];
            for (int gen = 0; gen < 3; gen++) {
                bool combo_ok = true;
                for (int iter = 0; iter < iterations; iter++) {
                    uint32_t gen_state = stress_state;
                    stress_stream(gen, STRESS_BLOCK_SIZE, &stress_state, decompressed_buffer, NULL);

                    tamp_res res;
                    TampCompressor compressor;
                    TampConf conf = {.window = wb, .literal = 8, .use_custom_dictionary = false};
                    res = tamp_compressor_init(&compressor, &conf, window_buffer);
                    size_t comp_size = 0;
                    if (res == TAMP_OK)
                        res = tamp_compressor_compress_and_flush(&compressor, compressed_buffer,
                                                                 sizeof(compressed_buffer), &comp_size,
                                                                 decompressed_buffer, STRESS_BLOCK_SIZE, NULL, false);

                    size_t decomp_size = 0;
                    if (res == TAMP_OK) {
                        TampDecompressor decompressor;
                        tamp_decompressor_init(&decompressor, NULL, window_buffer, wb);
                        tamp_decompressor_decompress(&decompressor, decompressed_buffer, sizeof(decompressed_buffer),
                                                     &decomp_size, compressed_buffer, comp_size, NULL);
                    }

                    bool ok = (res == TAMP_OK) && (decomp_size == STRESS_BLOCK_SIZE);
                    if (ok) {
                        uint32_t verify_state = gen_state;
                        ok = stress_stream(gen, STRESS_BLOCK_SIZE, &verify_state, NULL, decompressed_buffer) == 0;
                    }
                    if (!ok) {
                        combo_ok = false;
                        printf("FAIL: stress seed=%u w=%d gen=%d iter=%d (res=%d)\n", (unsigned)STRESS_SEED, wb, gen,
                               iter, res);
                        failures++;
                    }
                }
                if (combo_ok) printf("PASS: stress w=%d gen=%d (%d iters)\n", wb, gen, iterations);
            }
        }
    }

    if (failures == 0)
        printf("TAMP-DEVICE-RESULT: PASS\n");
    else
        printf("TAMP-DEVICE-RESULT: FAIL failures=%d\n", failures);
    fflush(stdout);

    return failures;
}
