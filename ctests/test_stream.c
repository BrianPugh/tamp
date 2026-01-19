#include <stddef.h>
#include <stdio.h>
#include <string.h>

// TAMP_STREAM_STDIO and TAMP_STREAM_MEMORY are defined via CFLAGS in the Makefile
#include "tamp/compressor.h"
#include "tamp/decompressor.h"
#include "unity.h"

void test_decompress_stream_simple(void) {
    // Pre-compressed "foo foo foo" data
    const unsigned char compressed[] = {
        0b01011000,  // header (window_bits=10, literal_bits=8)
        0b10110011,  // literal "f"
        0b00000100,  // the pre-init buffer contains "oo" at index 131
        0b00011100,  // literal " "
        0b10000001,  // There is now "foo " at index 0
        0b00000000,  // size=4 -> 0b1000
        0b00000011,  // Just "foo" at index 0; size=3 -> 0b11
        0b00000000,  // index 0 -> 0b0000000000
        0b00000000,  // 6 bits of zero-padding
    };

    // Setup memory streams
    TampMemReader reader = {.data = compressed, .size = sizeof(compressed), .pos = 0};

    unsigned char decompressed_data[64];
    TampMemWriter writer = {.data = decompressed_data, .capacity = sizeof(decompressed_data), .pos = 0};

    // Allocate buffers
    unsigned char window[1 << 10];

    // Initialize decompressor (conf=NULL to read from header)
    TampDecompressor decompressor;
    tamp_res res = tamp_decompressor_init(&decompressor, NULL, window, 10);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    size_t input_consumed, output_written;

    res = tamp_decompress_stream(&decompressor, tamp_stream_mem_read, &reader, tamp_stream_mem_write, &writer,
                                 &input_consumed, &output_written, NULL, NULL);

    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(sizeof(compressed), input_consumed);
    TEST_ASSERT_EQUAL(11, output_written);  // "foo foo foo" = 11 chars

    // Null-terminate for string comparison
    decompressed_data[output_written] = '\0';
    TEST_ASSERT_EQUAL_STRING("foo foo foo", (char *)decompressed_data);
}

void test_stream_roundtrip(void) {
    // Test compressing and then decompressing
    const char *original =
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog.";
    size_t original_len = strlen(original);

    // Step 1: Compress
    TampMemReader compress_reader = {.data = (const unsigned char *)original, .size = original_len, .pos = 0};

    unsigned char compressed[512];
    TampMemWriter compress_writer = {.data = compressed, .capacity = sizeof(compressed), .pos = 0};

    unsigned char window1[1 << 10];

    TampConf conf = {
        .window = 10,
        .literal = 8,
        .use_custom_dictionary = false,
    };

    // Initialize compressor
    TampCompressor compressor;
    tamp_res res = tamp_compressor_init(&compressor, &conf, window1);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    size_t compress_in, compress_out;
    res = tamp_compress_stream(&compressor, tamp_stream_mem_read, &compress_reader, tamp_stream_mem_write,
                               &compress_writer, &compress_in, &compress_out, NULL, NULL);

    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(original_len, compress_in);

    // Step 2: Decompress
    TampMemReader decompress_reader = {.data = compressed, .size = compress_out, .pos = 0};

    unsigned char decompressed[512];
    TampMemWriter decompress_writer = {.data = decompressed, .capacity = sizeof(decompressed), .pos = 0};

    unsigned char window2[1 << 10];

    // Initialize decompressor
    TampDecompressor decompressor;
    res = tamp_decompressor_init(&decompressor, NULL, window2, 10);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    size_t decompress_in, decompress_out;
    res = tamp_decompress_stream(&decompressor, tamp_stream_mem_read, &decompress_reader, tamp_stream_mem_write,
                                 &decompress_writer, &decompress_in, &decompress_out, NULL, NULL);

    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(compress_out, decompress_in);
    TEST_ASSERT_EQUAL(original_len, decompress_out);

    // Verify data matches
    TEST_ASSERT_EQUAL_MEMORY(original, decompressed, original_len);
}

void test_stdio_handlers_roundtrip(void) {
    // Test the built-in stdio handlers using temporary files
    const char *original =
        "Testing stdio handlers! "
        "This data should compress and decompress correctly. "
        "Testing stdio handlers! Testing stdio handlers!";
    size_t original_len = strlen(original);

    // Create temporary files
    FILE *temp_input = tmpfile();
    FILE *temp_compressed = tmpfile();
    FILE *temp_output = tmpfile();

    TEST_ASSERT_NOT_NULL(temp_input);
    TEST_ASSERT_NOT_NULL(temp_compressed);
    TEST_ASSERT_NOT_NULL(temp_output);

    // Write original data to input file
    fwrite(original, 1, original_len, temp_input);
    rewind(temp_input);

    // Compress using stdio handlers
    unsigned char window[1 << 10];

    TampConf conf = {
        .window = 10,
        .literal = 8,
        .use_custom_dictionary = false,
    };

    // Initialize compressor
    TampCompressor compressor;
    tamp_res res = tamp_compressor_init(&compressor, &conf, window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    size_t compress_in, compress_out;
    res = tamp_compress_stream(&compressor, tamp_stream_stdio_read, temp_input, tamp_stream_stdio_write,
                               temp_compressed, &compress_in, &compress_out, NULL, NULL);

    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(original_len, compress_in);
    TEST_ASSERT_GREATER_THAN(0, compress_out);

    // Rewind compressed file for reading
    rewind(temp_compressed);

    // Initialize decompressor
    TampDecompressor decompressor;
    res = tamp_decompressor_init(&decompressor, NULL, window, 10);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    // Decompress using stdio handlers
    size_t decompress_in, decompress_out;
    res = tamp_decompress_stream(&decompressor, tamp_stream_stdio_read, temp_compressed, tamp_stream_stdio_write,
                                 temp_output, &decompress_in, &decompress_out, NULL, NULL);

    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(compress_out, decompress_in);
    TEST_ASSERT_EQUAL(original_len, decompress_out);

    // Verify decompressed data
    rewind(temp_output);
    char decompressed[256];
    size_t read_back = fread(decompressed, 1, sizeof(decompressed), temp_output);
    TEST_ASSERT_EQUAL(original_len, read_back);
    TEST_ASSERT_EQUAL_MEMORY(original, decompressed, original_len);

    // Cleanup
    fclose(temp_input);
    fclose(temp_compressed);
    fclose(temp_output);
}
