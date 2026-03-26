#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "tamp/compressor.h"
#include "tamp/decompressor.h"
#include "unity.h"

// Callback tracking for compress_cb tests
typedef struct {
    size_t call_count;
    size_t last_bytes_processed;
    size_t expected_total_bytes;
} CallbackTracker;

static void callback_tracker_init(CallbackTracker *t, size_t expected_total) {
    t->call_count = 0;
    t->last_bytes_processed = 0;
    t->expected_total_bytes = expected_total;
}

static int tracking_callback(void *user_data, size_t bytes_processed, size_t total_bytes) {
    CallbackTracker *t = (CallbackTracker *)user_data;
    TEST_ASSERT_GREATER_OR_EQUAL(t->last_bytes_processed, bytes_processed);
    TEST_ASSERT_EQUAL(t->expected_total_bytes, total_bytes);
    t->last_bytes_processed = bytes_processed;
    t->call_count++;
    return 0;
}

static int abort_callback(void *user_data, size_t bytes_processed, size_t total_bytes) {
    (void)bytes_processed;
    (void)total_bytes;
    size_t *threshold = (size_t *)user_data;
    if (bytes_processed >= *threshold) return 100;  // custom error code
    return 0;
}

void test_compressor_init(void) {
    TampCompressor compressor;
    unsigned char window[1024];
    TampConf conf = {
        .window = 10,
        .literal = 8,
        .use_custom_dictionary = false,
    };

    tamp_res res = tamp_compressor_init(&compressor, &conf, window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
}

void test_compressor_simple(void) {
    tamp_res res;
    TampCompressor compressor;
    unsigned char window[1 << 10];
    unsigned char output[256];
    size_t output_written;
    size_t input_consumed;
    TampConf conf = {
        .window = 10,
        .literal = 8,
        .use_custom_dictionary = false,
    };

    res = tamp_compressor_init(&compressor, &conf, window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    const char *input = "foo foo foo";
    res = tamp_compressor_compress_and_flush(&compressor, output, sizeof(output), &output_written,
                                             (unsigned char *)input, strlen(input), &input_consumed, false);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_GREATER_THAN(0, output_written);
}

void test_compress_cb_callback_receives_input_consumed(void) {
    TampCompressor compressor;
    unsigned char window[1 << 10];
    unsigned char output[256];
    size_t output_written, input_consumed;
    TampConf conf = {.window = 10, .literal = 8};

    tamp_compressor_init(&compressor, &conf, window);

    const char *input =
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog.";
    size_t input_size = strlen(input);

    CallbackTracker tracker;
    callback_tracker_init(&tracker, input_size);

    tamp_res res = tamp_compressor_compress_and_flush_cb(&compressor, output, sizeof(output), &output_written,
                                                         (const unsigned char *)input, input_size, &input_consumed,
                                                         false, tracking_callback, &tracker);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(input_size, input_consumed);

    // Callback should have been called at least once
    TEST_ASSERT_GREATER_THAN(0, tracker.call_count);

    // Monotonicity and total_bytes consistency are checked in the callback itself.
    // Final callback should report 100% (bytes_processed == total_bytes == input_size)
    TEST_ASSERT_EQUAL(input_size, tracker.last_bytes_processed);
}

void test_compressor_extended_simple(void) {
    tamp_res res;
    TampCompressor compressor;
    unsigned char window[1 << 10];
    unsigned char output[256];
    size_t output_written;
    size_t input_consumed;
    TampConf conf = {
        .window = 10,
        .literal = 8,
        .use_custom_dictionary = false,
        .extended = true,
    };

    res = tamp_compressor_init(&compressor, &conf, window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    const char *input = "foo foo foo";
    res = tamp_compressor_compress_and_flush(&compressor, output, sizeof(output), &output_written,
                                             (unsigned char *)input, strlen(input), &input_consumed, false);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_GREATER_THAN(0, output_written);
    // Verify the extended bit is set in the header
    TEST_ASSERT_EQUAL(0x02, output[0] & 0x02);
}

void test_compressor_extended_rle_roundtrip(void) {
    // Compress repeated bytes with extended=true, then decompress and verify
    tamp_res res;
    TampCompressor compressor;
    unsigned char c_window[1 << 10];
    unsigned char compressed[256];
    size_t compressed_size;
    size_t input_consumed;
    TampConf conf = {
        .window = 10,
        .literal = 8,
        .extended = true,
    };

    res = tamp_compressor_init(&compressor, &conf, c_window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    // 200 bytes of 'A' - should trigger RLE encoding
    unsigned char input[200];
    memset(input, 'A', sizeof(input));

    res = tamp_compressor_compress_and_flush(&compressor, compressed, sizeof(compressed), &compressed_size, input,
                                             sizeof(input), &input_consumed, false);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(sizeof(input), input_consumed);
    // RLE should compress extremely well (200 x 'A' -> ~5 bytes)
    TEST_ASSERT_LESS_THAN(10, compressed_size);

    // Decompress and verify
    TampDecompressor decompressor;
    unsigned char d_window[1 << 10];
    unsigned char output[256];
    size_t output_written;

    res = tamp_decompressor_init(&decompressor, NULL, d_window, 10);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    res = tamp_decompressor_decompress(&decompressor, output, sizeof(output), &output_written, compressed,
                                       compressed_size, NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(sizeof(input), output_written);
    TEST_ASSERT_EQUAL_MEMORY(input, output, sizeof(input));
}

void test_compressor_extended_match_roundtrip(void) {
    // Compress data with repeating patterns that trigger extended match tokens
    tamp_res res;
    TampCompressor compressor;
    unsigned char c_window[1 << 10];
    unsigned char compressed[512];
    size_t compressed_size;
    size_t input_consumed;
    TampConf conf = {
        .window = 10,
        .literal = 8,
        .extended = true,
    };

    res = tamp_compressor_init(&compressor, &conf, c_window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    // Repeating 16-byte pattern triggers extended match (> min_pattern_size + 11 = 13)
    const char *pattern = "Hello, World!!! ";  // 16 bytes
    unsigned char input[320];                  // 20 repetitions
    for (size_t i = 0; i < sizeof(input); i++) {
        input[i] = pattern[i % 16];
    }

    res = tamp_compressor_compress_and_flush(&compressor, compressed, sizeof(compressed), &compressed_size, input,
                                             sizeof(input), &input_consumed, false);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(sizeof(input), input_consumed);
    // Extended match should compress 320 bytes of repeating 16-byte pattern well (~34 bytes)
    TEST_ASSERT_LESS_THAN(50, compressed_size);

    // Decompress and verify
    TampDecompressor decompressor;
    unsigned char d_window[1 << 10];
    unsigned char output[512];
    size_t output_written;

    res = tamp_decompressor_init(&decompressor, NULL, d_window, 10);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    res = tamp_decompressor_decompress(&decompressor, output, sizeof(output), &output_written, compressed,
                                       compressed_size, NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(sizeof(input), output_written);
    TEST_ASSERT_EQUAL_MEMORY(input, output, sizeof(input));
}

#if TAMP_LAZY_MATCHING
void test_compressor_extended_lazy_roundtrip(void) {
    // Test extended + lazy_matching combined
    tamp_res res;
    TampCompressor compressor;
    unsigned char c_window[1 << 10];
    unsigned char compressed[512];
    size_t compressed_size;
    size_t input_consumed;
    TampConf conf = {
        .window = 10,
        .literal = 8,
        .extended = true,
        .lazy_matching = true,
    };

    res = tamp_compressor_init(&compressor, &conf, c_window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    const char *input =
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog.";
    size_t input_size = strlen(input);

    res = tamp_compressor_compress_and_flush(&compressor, compressed, sizeof(compressed), &compressed_size,
                                             (const unsigned char *)input, input_size, &input_consumed, false);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(input_size, input_consumed);

    // Decompress and verify
    TampDecompressor decompressor;
    unsigned char d_window[1 << 10];
    unsigned char output[512];
    size_t output_written;

    res = tamp_decompressor_init(&decompressor, NULL, d_window, 10);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    res = tamp_decompressor_decompress(&decompressor, output, sizeof(output), &output_written, compressed,
                                       compressed_size, NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(input_size, output_written);
    TEST_ASSERT_EQUAL_MEMORY(input, output, input_size);
}
#endif  // TAMP_LAZY_MATCHING

void test_compressor_extended_rle_transition_roundtrip(void) {
    // Test data that transitions between RLE runs and non-RLE content
    tamp_res res;
    TampCompressor compressor;
    unsigned char c_window[1 << 10];
    unsigned char compressed[256];
    size_t compressed_size;
    size_t input_consumed;
    TampConf conf = {
        .window = 10,
        .literal = 8,
        .extended = true,
    };

    res = tamp_compressor_init(&compressor, &conf, c_window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    // RLE run + non-repeating + RLE run
    unsigned char input[120];
    memset(input, 'A', 50);                                // 50 x 'A'
    memcpy(input + 50, "The quick brown fox jumps!", 25);  // 25 bytes mixed
    memset(input + 75, 'B', 45);                           // 45 x 'B'

    res = tamp_compressor_compress_and_flush(&compressor, compressed, sizeof(compressed), &compressed_size, input,
                                             sizeof(input), &input_consumed, false);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(sizeof(input), input_consumed);

    // Decompress and verify
    TampDecompressor decompressor;
    unsigned char d_window[1 << 10];
    unsigned char output[256];
    size_t output_written;

    res = tamp_decompressor_init(&decompressor, NULL, d_window, 10);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    res = tamp_decompressor_decompress(&decompressor, output, sizeof(output), &output_written, compressed,
                                       compressed_size, NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(sizeof(input), output_written);
    TEST_ASSERT_EQUAL_MEMORY(input, output, sizeof(input));
}

void test_compressor_extended_window8_roundtrip(void) {
    // Test extended mode with window=8
    tamp_res res;
    TampCompressor compressor;
    unsigned char c_window[1 << 8];
    unsigned char compressed[512];
    size_t compressed_size;
    size_t input_consumed;
    TampConf conf = {
        .window = 8,
        .literal = 8,
        .extended = true,
    };

    res = tamp_compressor_init(&compressor, &conf, c_window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    const char *input =
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog.";
    size_t input_size = strlen(input);

    res = tamp_compressor_compress_and_flush(&compressor, compressed, sizeof(compressed), &compressed_size,
                                             (const unsigned char *)input, input_size, &input_consumed, false);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(input_size, input_consumed);

    // Decompress and verify
    TampDecompressor decompressor;
    unsigned char d_window[1 << 8];
    unsigned char output[512];
    size_t output_written;

    res = tamp_decompressor_init(&decompressor, NULL, d_window, 8);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    res = tamp_decompressor_decompress(&decompressor, output, sizeof(output), &output_written, compressed,
                                       compressed_size, NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(input_size, output_written);
    TEST_ASSERT_EQUAL_MEMORY(input, output, input_size);
}

void test_compressor_extended_window9_roundtrip(void) {
    // Test extended mode with window=9
    tamp_res res;
    TampCompressor compressor;
    unsigned char c_window[1 << 9];
    unsigned char compressed[256];
    size_t compressed_size;
    size_t input_consumed;
    TampConf conf = {
        .window = 9,
        .literal = 8,
        .extended = true,
    };

    res = tamp_compressor_init(&compressor, &conf, c_window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    // RLE with smaller window
    unsigned char input[200];
    memset(input, 'Q', sizeof(input));

    res = tamp_compressor_compress_and_flush(&compressor, compressed, sizeof(compressed), &compressed_size, input,
                                             sizeof(input), &input_consumed, false);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(sizeof(input), input_consumed);
    TEST_ASSERT_LESS_THAN(10, compressed_size);

    // Decompress and verify
    TampDecompressor decompressor;
    unsigned char d_window[1 << 9];
    unsigned char output[256];
    size_t output_written;

    res = tamp_decompressor_init(&decompressor, NULL, d_window, 9);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    res = tamp_decompressor_decompress(&decompressor, output, sizeof(output), &output_written, compressed,
                                       compressed_size, NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(sizeof(input), output_written);
    TEST_ASSERT_EQUAL_MEMORY(input, output, sizeof(input));
}

void test_reset_dictionary_roundtrip(void) {
    // Basic reset dictionary roundtrip: compress, reset, compress more, decompress all.
    tamp_res res;
    TampCompressor compressor;
    unsigned char c_window[1 << 10];
    unsigned char compressed[512];
    size_t total_written = 0;
    size_t chunk_written, input_consumed;
    TampConf conf = {
        .window = 10,
        .literal = 8,
        .extended = true,
        .dictionary_reset = true,
    };

    res = tamp_compressor_init(&compressor, &conf, c_window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    // Compress first chunk
    const char *data1 = "Hello world! Hello world! Hello world! ";
    res = tamp_compressor_compress_and_flush(&compressor, compressed, sizeof(compressed), &chunk_written,
                                             (const unsigned char *)data1, strlen(data1), &input_consumed, true);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    total_written += chunk_written;

    // Reset dictionary
    res = tamp_compressor_reset_dictionary(&compressor, compressed + total_written, sizeof(compressed) - total_written,
                                           &chunk_written);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    total_written += chunk_written;

    // Compress second chunk
    const char *data2 = "Goodbye world! Goodbye world! Goodbye world! ";
    res = tamp_compressor_compress_and_flush(&compressor, compressed + total_written,
                                             sizeof(compressed) - total_written, &chunk_written,
                                             (const unsigned char *)data2, strlen(data2), &input_consumed, false);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    total_written += chunk_written;

    // Decompress and verify
    TampDecompressor decompressor;
    unsigned char d_window[1 << 10];
    unsigned char output[512];
    size_t output_written;

    res = tamp_decompressor_init(&decompressor, NULL, d_window, 10);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    res = tamp_decompressor_decompress(&decompressor, output, sizeof(output), &output_written, compressed,
                                       total_written, NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(TAMP_OK, res);
    size_t expected_size = strlen(data1) + strlen(data2);
    TEST_ASSERT_EQUAL(expected_size, output_written);
    TEST_ASSERT_EQUAL_MEMORY(data1, output, strlen(data1));
    TEST_ASSERT_EQUAL_MEMORY(data2, output + strlen(data1), strlen(data2));
}

void test_reset_dictionary_requires_conf_flag(void) {
    // reset_dictionary must fail if dictionary_reset was not set at init.
    tamp_res res;
    TampCompressor compressor;
    unsigned char window[1 << 10];
    unsigned char output[64];
    size_t output_written;
    TampConf conf = {
        .window = 10,
        .literal = 8,
        .dictionary_reset = false,
    };

    res = tamp_compressor_init(&compressor, &conf, window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    res = tamp_compressor_reset_dictionary(&compressor, output, sizeof(output), &output_written);
    TEST_ASSERT_EQUAL(TAMP_INVALID_CONF, res);
}

void test_reset_dictionary_small_output_buffer(void) {
    // If reset_dictionary gets TAMP_OUTPUT_FULL, retrying should still produce
    // a valid stream. The retry may emit an extra FLUSH (3 total), which
    // triggers a harmless redundant reset.
    tamp_res res;
    TampCompressor compressor;
    unsigned char c_window[1 << 10];
    unsigned char compressed[512];
    size_t total_written = 0;
    size_t chunk_written, input_consumed;
    TampConf conf = {
        .window = 10,
        .literal = 8,
        .extended = true,
        .dictionary_reset = true,
    };

    res = tamp_compressor_init(&compressor, &conf, c_window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    // Compress some data so there's pending state to flush.
    const char *data1 = "The quick brown fox jumps over the lazy dog. ";
    res = tamp_compressor_compress(&compressor, compressed, sizeof(compressed), &chunk_written,
                                   (const unsigned char *)data1, strlen(data1), &input_consumed);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    total_written += chunk_written;

    // Try reset with a tiny buffer — should fail with TAMP_OUTPUT_FULL.
    res = tamp_compressor_reset_dictionary(&compressor, compressed + total_written, 1, &chunk_written);
    TEST_ASSERT_EQUAL(TAMP_OUTPUT_FULL, res);
    // Note: some flush output may have been written even on failure.
    total_written += chunk_written;

    // Retry with enough space — should succeed.
    res = tamp_compressor_reset_dictionary(&compressor, compressed + total_written, sizeof(compressed) - total_written,
                                           &chunk_written);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    total_written += chunk_written;

    // Compress more data after reset.
    const char *data2 = "New data after reset! New data after reset! ";
    res = tamp_compressor_compress_and_flush(&compressor, compressed + total_written,
                                             sizeof(compressed) - total_written, &chunk_written,
                                             (const unsigned char *)data2, strlen(data2), &input_consumed, false);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    total_written += chunk_written;

    // Decompress and verify the full stream is valid.
    TampDecompressor decompressor;
    unsigned char d_window[1 << 10];
    unsigned char output[512];
    size_t output_written;

    res = tamp_decompressor_init(&decompressor, NULL, d_window, 10);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    res = tamp_decompressor_decompress(&decompressor, output, sizeof(output), &output_written, compressed,
                                       total_written, NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(TAMP_OK, res);

    // The output should contain data1 + data2 (the extra reset is harmless).
    size_t expected_size = strlen(data1) + strlen(data2);
    TEST_ASSERT_EQUAL(expected_size, output_written);
    TEST_ASSERT_EQUAL_MEMORY(data1, output, strlen(data1));
    TEST_ASSERT_EQUAL_MEMORY(data2, output + strlen(data1), strlen(data2));
}

void test_compress_cb_callback_abort(void) {
    TampCompressor compressor;
    unsigned char window[1 << 10];
    unsigned char output[256];
    size_t output_written, input_consumed;
    TampConf conf = {.window = 10, .literal = 8};

    tamp_compressor_init(&compressor, &conf, window);

    const char *input =
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog.";
    size_t input_size = strlen(input);

    size_t threshold = 1;  // abort after consuming any input
    tamp_res res =
        tamp_compressor_compress_cb(&compressor, output, sizeof(output), &output_written, (const unsigned char *)input,
                                    input_size, &input_consumed, abort_callback, &threshold);
    TEST_ASSERT_EQUAL(100, res);
    // Should have consumed less than all input
    TEST_ASSERT_LESS_THAN(input_size, input_consumed);
}
