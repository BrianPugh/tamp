#include <stdio.h>
#include <string.h>

#include "enwik8.h"
#include "enwik8_compressed.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "profiling.h"
#include "tamp/compressor.h"
#include "tamp/decompressor.h"

TampProfilingStats tamp_profiling_stats;

static unsigned char window_buffer[1024];
static unsigned char output_buffer[100 << 10];

static int benchmark_compressor(size_t *compressed_size) {
    TampCompressor compressor;
    TampConf compressor_conf = {.literal = 8, .window = 10, .use_custom_dictionary = false};

    if (TAMP_OK != tamp_compressor_init(&compressor, &compressor_conf, window_buffer)) return -1;

    if (TAMP_OK != tamp_compressor_compress_and_flush(&compressor, output_buffer, sizeof(output_buffer),
                                                      compressed_size, ENWIK8, sizeof(ENWIK8), NULL, false))
        return -2;
    return 0;
}

static int benchmark_decompressor(size_t *output_written_size) {
    TampDecompressor decompressor;

    if (TAMP_OK != tamp_decompressor_init(&decompressor, NULL, window_buffer, 10)) return -1;

    if (0 > tamp_decompressor_decompress(&decompressor, output_buffer, sizeof(output_buffer), output_written_size,
                                         ENWIK8_COMPRESSED, sizeof(ENWIK8_COMPRESSED), NULL))
        return -2;

    return 0;
}

static void print_profiling_stats(void) {
    printf("  Profiling breakdown:\n");
    printf("    bit_buffer_fill: %llu us\n", tamp_profiling_stats.bit_buffer_fill_time_us);
    printf("    literal:         %llu us (%lu count)\n", tamp_profiling_stats.literal_time_us,
           tamp_profiling_stats.literal_count);
    printf("    pattern_decode:  %llu us\n", tamp_profiling_stats.pattern_decode_time_us);
    printf("    pattern_output:  %llu us\n", tamp_profiling_stats.pattern_output_time_us);
    printf("    window_update:   %llu us (%lu patterns, %lu bytes avg)\n", tamp_profiling_stats.window_update_time_us,
           tamp_profiling_stats.pattern_count,
           tamp_profiling_stats.pattern_count
               ? tamp_profiling_stats.pattern_bytes_total / tamp_profiling_stats.pattern_count
               : 0);
    printf("    overlap: %lu, non_overlap: %lu (%.1f%% overlap)\n", tamp_profiling_stats.overlap_count,
           tamp_profiling_stats.non_overlap_count,
           tamp_profiling_stats.pattern_count
               ? 100.0f * tamp_profiling_stats.overlap_count / tamp_profiling_stats.pattern_count
               : 0.0f);
}

int main() {
    stdio_init_all();

    while (true) {
        int failures = 0;
        int res;
        size_t output_size;
        absolute_time_t start_time, end_time;
        int64_t elapsed_us;
        uint32_t bytes_per_sec;

        {
            start_time = get_absolute_time();
            res = benchmark_compressor(&output_size);
            end_time = get_absolute_time();

            elapsed_us = absolute_time_diff_us(start_time, end_time);
            bytes_per_sec = (uint32_t)((uint64_t)sizeof(ENWIK8) * 1000000 / elapsed_us);
            printf("BENCH compress_enwik8_us=%lld\n", elapsed_us);
            printf("INFO compressed_size=%u compress_bytes_per_sec=%lu\n", (unsigned)output_size, bytes_per_sec);
            if (res != 0) {
                printf("FAIL: compress enwik8 (res=%d)\n", res);
                failures++;
            } else if (output_size != sizeof(ENWIK8_COMPRESSED) ||
                       memcmp(output_buffer, ENWIK8_COMPRESSED, sizeof(ENWIK8_COMPRESSED))) {
                printf("FAIL: compress enwik8 matches reference (expected %u bytes, got %u)\n",
                       (unsigned)sizeof(ENWIK8_COMPRESSED), (unsigned)output_size);
                failures++;
            } else {
                printf("PASS: compress enwik8 matches reference\n");
            }
        }

        {
            tamp_profiling_reset();
            start_time = get_absolute_time();
            res = benchmark_decompressor(&output_size);
            end_time = get_absolute_time();

            elapsed_us = absolute_time_diff_us(start_time, end_time);
            bytes_per_sec = (uint32_t)((uint64_t)sizeof(ENWIK8) * 1000000 / elapsed_us);
            printf("BENCH decompress_enwik8_us=%lld\n", elapsed_us);
            printf("INFO decompress_bytes_per_sec=%lu\n", bytes_per_sec);
            if (res != 0) {
                printf("FAIL: decompress enwik8 (res=%d)\n", res);
                failures++;
            } else if (output_size != sizeof(ENWIK8) || memcmp(output_buffer, ENWIK8, sizeof(ENWIK8))) {
                printf("FAIL: decompress reference matches original (expected %u bytes, got %u)\n",
                       (unsigned)sizeof(ENWIK8), (unsigned)output_size);
                failures++;
            } else {
                printf("PASS: decompress reference matches original\n");
            }
            print_profiling_stats();
        }

        if (failures == 0)
            printf("TAMP-DEVICE-RESULT: PASS\n");
        else
            printf("TAMP-DEVICE-RESULT: FAIL failures=%d\n", failures);

        sleep_ms(2000);
    }
}
