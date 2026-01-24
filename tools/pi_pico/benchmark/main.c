#include <stdio.h>

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

int benchmark_compressor() {
    size_t compressed_size = 0;

    TampCompressor compressor;
    TampConf compressor_conf = {.literal = 8, .window = 10, .use_custom_dictionary = false};

    if (TAMP_OK != tamp_compressor_init(&compressor, &compressor_conf, window_buffer)) return -1;

    if (TAMP_OK != tamp_compressor_compress_and_flush(&compressor, output_buffer, sizeof(output_buffer),
                                                      &compressed_size, ENWIK8, sizeof(ENWIK8), NULL, false))
        return -2;
    return compressed_size;
}

int benchmark_decompressor() {
    TampDecompressor decompressor;
    int output_written_size;

    if (TAMP_OK != tamp_decompressor_init(&decompressor, NULL, window_buffer, 10)) return -1;

    if (0 > tamp_decompressor_decompress(&decompressor, output_buffer, sizeof(output_buffer), &output_written_size,
                                         ENWIK8_COMPRESSED, sizeof(ENWIK8_COMPRESSED), NULL))
        return -2;

    return output_written_size;
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
    // Initialise I/O
    stdio_init_all();

    while (true) {
        int output_size;
        absolute_time_t start_time, end_time;
        int64_t elapsed_us;
        uint32_t bytes_per_sec;

        {
            start_time = get_absolute_time();
            output_size = benchmark_compressor();
            end_time = get_absolute_time();

            elapsed_us = absolute_time_diff_us(start_time, end_time);
            bytes_per_sec = (uint32_t)((uint64_t)sizeof(ENWIK8) * 1000000 / elapsed_us);
            printf("compression: %lld us, %lu bytes/s\n", elapsed_us, bytes_per_sec);
        }
        if (output_size != sizeof(ENWIK8_COMPRESSED)) printf("Unexpected compressed size: %d\n", output_size);

        {
            tamp_profiling_reset();
            start_time = get_absolute_time();
            output_size = benchmark_decompressor();
            end_time = get_absolute_time();

            elapsed_us = absolute_time_diff_us(start_time, end_time);
            bytes_per_sec = (uint32_t)((uint64_t)sizeof(ENWIK8) * 1000000 / elapsed_us);
            printf("decompression: %lld us, %lu bytes/s\n", elapsed_us, bytes_per_sec);
            print_profiling_stats();
        }
        if (output_size != sizeof(ENWIK8)) printf("Unexpected decompressed size: %d\n", output_size);

        sleep_ms(1000);
    }
}
