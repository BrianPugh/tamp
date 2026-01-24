/**
 * Stream API benchmark for measuring the impact of temporary working buffer sizes.
 *
 * The stream functions (tamp_compress_stream, tamp_decompress_stream) use stack-allocated
 * temporary buffers for I/O operations. The size of these buffers is controlled by
 * TAMP_STREAM_WORK_BUFFER_SIZE, which is passed via -D at compile time by the Makefile.
 *
 * Usage: make c-benchmark-stream
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TAMP_STREAM_STDIO 1
#include "tamp/compressor.h"
#include "tamp/decompressor.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    const char *input_file = argv[1];
    const char *compressed_file = "/tmp/benchmark_compressed.tamp";
    const char *decompressed_file = "/tmp/benchmark_decompressed.bin";

    unsigned char window[1 << 10];  // 1KB window

    // Compression benchmark
    {
        FILE *in = fopen(input_file, "rb");
        FILE *out = fopen(compressed_file, "wb");
        if (!in || !out) {
            fprintf(stderr, "Failed to open files for compression\n");
            return 1;
        }

        TampCompressor compressor;
        tamp_compressor_init(&compressor, NULL, window);

        size_t input_consumed, output_written;
        clock_t start = clock();

        tamp_res res = tamp_compress_stream(&compressor, tamp_stream_stdio_read, in, tamp_stream_stdio_write, out,
                                            &input_consumed, &output_written, NULL, NULL);

        clock_t end = clock();
        double compress_time = (double)(end - start) / CLOCKS_PER_SEC;

        fclose(in);
        fclose(out);

        if (res != TAMP_OK) {
            fprintf(stderr, "Compression failed: %d\n", res);
            return 1;
        }

        printf("Compression: %.3fs, %zu -> %zu bytes (%.1f%%)\n", compress_time, input_consumed, output_written,
               100.0 * output_written / input_consumed);
    }

    // Decompression benchmark
    {
        FILE *in = fopen(compressed_file, "rb");
        FILE *out = fopen(decompressed_file, "wb");
        if (!in || !out) {
            fprintf(stderr, "Failed to open files for decompression\n");
            return 1;
        }

        TampDecompressor decompressor;
        tamp_decompressor_init(&decompressor, NULL, window, 10);

        size_t input_consumed, output_written;
        clock_t start = clock();

        tamp_res res = tamp_decompress_stream(&decompressor, tamp_stream_stdio_read, in, tamp_stream_stdio_write, out,
                                              &input_consumed, &output_written, NULL, NULL);

        clock_t end = clock();
        double decompress_time = (double)(end - start) / CLOCKS_PER_SEC;

        fclose(in);
        fclose(out);

        if (res != TAMP_OK) {
            fprintf(stderr, "Decompression failed: %d\n", res);
            return 1;
        }

        printf("Decompression: %.3fs, %zu -> %zu bytes\n", decompress_time, input_consumed, output_written);
    }

    // Cleanup
    remove(compressed_file);
    remove(decompressed_file);

    return 0;
}
