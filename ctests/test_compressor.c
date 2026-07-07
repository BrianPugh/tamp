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

void test_double_flush_does_not_reset(void) {
    // A redundant flush(write_token=true) must NOT emit a second consecutive FLUSH.
    // With dictionary_reset=true, two FLUSH tokens with no data between them are the
    // double-FLUSH reset signal: the decompressor would re-initialize its window while
    // this compressor's window stays put, corrupting all later output. The redundant
    // FLUSH must be suppressed so the stream round-trips cleanly.
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

    const char *data1 = "Hello world! Hello world! Hello world! ";
    res = tamp_compressor_compress(&compressor, compressed, sizeof(compressed), &chunk_written,
                                   (const unsigned char *)data1, strlen(data1), &input_consumed);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    total_written += chunk_written;

    // First flush emits a FLUSH; the next two redundant flushes must emit nothing.
    res = tamp_compressor_flush(&compressor, compressed + total_written, sizeof(compressed) - total_written,
                                &chunk_written, true);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    total_written += chunk_written;

    for (int i = 0; i < 2; i++) {
        res = tamp_compressor_flush(&compressor, compressed + total_written, sizeof(compressed) - total_written,
                                    &chunk_written, true);
        TEST_ASSERT_EQUAL(TAMP_OK, res);
        TEST_ASSERT_EQUAL(0, chunk_written);  // redundant FLUSH suppressed
        total_written += chunk_written;
    }

    const char *data2 = "Goodbye world! Goodbye world! Goodbye world! ";
    res = tamp_compressor_compress_and_flush(&compressor, compressed + total_written,
                                             sizeof(compressed) - total_written, &chunk_written,
                                             (const unsigned char *)data2, strlen(data2), &input_consumed, false);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    total_written += chunk_written;

    // Decompress: must recover data1 + data2 with no dictionary reset in between.
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

void test_compressor_extended_rle_lone_byte_sink_poll(void) {
    // Regression: a lone run byte accumulated into rle_count by one poll was
    // silently dropped when a later poll saw the run had ended (byte-at-a-time
    // sink/poll usage). "AAB" previously decompressed to "AB".
    tamp_res res;
    TampCompressor compressor;
    unsigned char c_window[1 << 10];
    unsigned char compressed[64];
    size_t compressed_size = 0;
    TampConf conf = {
        .window = 10,
        .literal = 8,
        .extended = true,
    };

    res = tamp_compressor_init(&compressor, &conf, c_window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    const unsigned char payload[3] = {'A', 'A', 'B'};
    for (size_t i = 0; i < sizeof(payload); i++) {
        size_t written = 0;
        tamp_compressor_sink(&compressor, &payload[i], 1, NULL);
        res = tamp_compressor_poll(&compressor, compressed + compressed_size, sizeof(compressed) - compressed_size,
                                   &written);
        TEST_ASSERT_EQUAL(TAMP_OK, res);
        compressed_size += written;
    }
    {
        size_t written = 0;
        res = tamp_compressor_flush(&compressor, compressed + compressed_size, sizeof(compressed) - compressed_size,
                                    &written, false);
        TEST_ASSERT_EQUAL(TAMP_OK, res);
        compressed_size += written;
    }

    TampDecompressor decompressor;
    unsigned char d_window[1 << 10];
    unsigned char output[16];
    size_t output_written = 0;

    res = tamp_decompressor_init(&decompressor, NULL, d_window, 10);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    res = tamp_decompressor_decompress(&decompressor, output, sizeof(output), &output_written, compressed,
                                       compressed_size, NULL);
    // On success, decompress returns TAMP_INPUT_EXHAUSTED (all input consumed)
    // in lieu of TAMP_OK; the output buffer cannot fill on this payload.
    TEST_ASSERT_EQUAL(TAMP_INPUT_EXHAUSTED, res);
    TEST_ASSERT_EQUAL(sizeof(payload), output_written);
    TEST_ASSERT_EQUAL_MEMORY(payload, output, sizeof(payload));
}

#if TAMP_LAZY_MATCHING
void test_compressor_extended_lazy_rle_fuzz(void) {
    // Regression: with lazy_matching + extended, the RLE path consumed input
    // without invalidating the cached lazy match, corrupting output.
    // Generator: two-letter alphabet with short runs and 1-2 byte words, using
    // the same fixed LCG as the Python regression test so the corpus is
    // identical across toolchains and failing seeds are reproducible; on the
    // pre-fix code this corrupts ~5-10% of seeds.
    static unsigned char data[512];
    static unsigned char compressed[8192];
    static unsigned char output[4096];
    unsigned char c_window[1 << 10];
    unsigned char d_window[1 << 10];

    for (unsigned int seed = 0; seed < 1000; seed++) {
        unsigned int state = seed * 2 + 12345;
        size_t len = 0;
        while (len < sizeof(data)) {
            state = (state * 1103515245u + 12345u) & 0x7FFFFFFFu;
            unsigned int r = state >> 7;
            unsigned char b = (unsigned char)('a' + (r & 1));
            if (r & 2) {
                // Short run of one letter.
                unsigned int run = 1 + ((r >> 2) & 3);
                for (unsigned int j = 0; j < run && len < sizeof(data); j++) data[len++] = b;
            } else if (r & 4) {
                // Two letters.
                data[len++] = b;
                if (len < sizeof(data)) data[len++] = (unsigned char)('a' + ((r >> 3) & 1));
            } else {
                // One letter.
                data[len++] = b;
            }
        }
        len = sizeof(data);

        TampCompressor compressor;
        TampConf conf = {
            .window = 10,
            .literal = 8,
            .extended = true,
            .lazy_matching = true,
        };
        TEST_ASSERT_EQUAL(TAMP_OK, tamp_compressor_init(&compressor, &conf, c_window));

        size_t compressed_size = 0, input_consumed = 0;
        tamp_res res = tamp_compressor_compress_and_flush(&compressor, compressed, sizeof(compressed), &compressed_size,
                                                          data, len, &input_consumed, false);
        TEST_ASSERT_EQUAL(TAMP_OK, res);

        TampDecompressor decompressor;
        TEST_ASSERT_EQUAL(TAMP_OK, tamp_decompressor_init(&decompressor, NULL, d_window, 10));
        size_t output_written = 0;
        res = tamp_decompressor_decompress(&decompressor, output, sizeof(output), &output_written, compressed,
                                           compressed_size, NULL);
        // On success, decompress returns TAMP_INPUT_EXHAUSTED in lieu of TAMP_OK.
        TEST_ASSERT_EQUAL(TAMP_INPUT_EXHAUSTED, res);
        TEST_ASSERT_EQUAL_MESSAGE(len, output_written, "roundtrip length mismatch");
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data, output, len, "roundtrip data mismatch");
    }
}
#endif  // TAMP_LAZY_MATCHING

/*
 * find_best_match edge cases.
 *
 * Each case pins the exact compressed bytes (goldens generated from the
 * pure-Python reference implementation), so a match-finder change that
 * degrades match quality — while still producing a valid, decompressible
 * stream — fails loudly. They run against whichever find_best_match
 * implementation this binary was built with (desktop, or embedded via
 * TAMP_USE_EMBEDDED_MATCH=1 / `make c-test-embedded`).
 */

static void run_match_finder_case(const unsigned char *dictionary, const unsigned char *input, size_t input_size,
                                  const unsigned char *expected, size_t expected_size) {
    tamp_res res;
    TampCompressor compressor;
    unsigned char c_window[1 << 8];
    unsigned char compressed[64];
    unsigned char decompressed[64];
    size_t compressed_size, input_consumed, decompressed_size;
    TampConf conf = {
        .window = 8,
        .literal = 8,
        .use_custom_dictionary = true,
    };

    memcpy(c_window, dictionary, sizeof(c_window));
    res = tamp_compressor_init(&compressor, &conf, c_window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    res = tamp_compressor_compress_and_flush(&compressor, compressed, sizeof(compressed), &compressed_size, input,
                                             input_size, &input_consumed, false);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(input_size, input_consumed);
    TEST_ASSERT_EQUAL(expected_size, compressed_size);
    TEST_ASSERT_EQUAL_MEMORY(expected, compressed, expected_size);

    // Round-trip with a fresh copy of the dictionary.
    TampDecompressor decompressor;
    unsigned char d_window[1 << 8];
    memcpy(d_window, dictionary, sizeof(d_window));
    res = tamp_decompressor_init(&decompressor, NULL, d_window, 8);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    res = tamp_decompressor_decompress(&decompressor, decompressed, sizeof(decompressed), &decompressed_size,
                                       compressed, compressed_size, NULL);
    TEST_ASSERT_EQUAL(TAMP_INPUT_EXHAUSTED, res);
    TEST_ASSERT_EQUAL(input_size, decompressed_size);
    TEST_ASSERT_EQUAL_MEMORY(input, decompressed, input_size);
}

void test_find_best_match_window_edge(void) {
    // Best match ("WXYZ" at index 252) ends exactly at the last window byte;
    // the trailing "!!" forces the extension loop against the window boundary.
    unsigned char dictionary[1 << 8];
    memset(dictionary, 'a', sizeof(dictionary));
    memcpy(&dictionary[250], "UVWXYZ", 6);
    static const unsigned char expected[] = {0x1c, 0x47, 0xe4, 0x86, 0x42};
    run_match_finder_case(dictionary, (const unsigned char *)"WXYZ!!", 6, expected, sizeof(expected));
}

void test_find_best_match_alignment_phases(void) {
    // Progressively better matches at indices 3, 13, 26, 40 — one for each
    // word-alignment phase — so the first-byte scan must find candidates at
    // every alignment and update the best match each time.
    unsigned char dictionary[1 << 8];
    memset(dictionary, 0xFF, sizeof(dictionary));
    memcpy(&dictionary[3], "Qa", 2);
    memcpy(&dictionary[13], "Qab", 3);
    memcpy(&dictionary[26], "Qabc", 4);
    memcpy(&dictionary[40], "Qabcd", 5);
    static const unsigned char expected[] = {0x1c, 0x59, 0x40};
    run_match_finder_case(dictionary, (const unsigned char *)"Qabcd", 5, expected, sizeof(expected));
}

void test_find_best_match_swar_byte_patterns(void) {
    // Byte values that stress word-at-a-time (SWAR) zero-byte detection:
    // first byte 0x00, plus 0x00/0x7F/0x80 sequences that trigger the
    // borrow-propagation false-positive paths.
    unsigned char dictionary[1 << 8];
    memset(dictionary, 0x01, sizeof(dictionary));
    memcpy(&dictionary[10], "\x00\x7f", 2);
    memcpy(&dictionary[50], "\x80\x00\x80", 3);
    memcpy(&dictionary[100], "\x00\x00\x00", 3);
    memcpy(&dictionary[200], "\x00\x80\x7f\x01\xff", 5);
    static const unsigned char expected[] = {0x1c, 0x5e, 0x40};
    run_match_finder_case(dictionary, (const unsigned char *)"\x00\x80\x7f\x01\xff", 5, expected, sizeof(expected));
}

void test_find_best_match_max_pattern_early_exit(void) {
    // A full max_pattern_size (15-byte) match at index 30 triggers the
    // early-exit path; the 16th input byte is emitted as a literal.
    unsigned char dictionary[1 << 8];
    memset(dictionary, 'z', sizeof(dictionary));
    memcpy(&dictionary[30], "ABCDEFGHIJKLMNOP", 16);
    static const unsigned char expected[] = {0x1c, 0x4e, 0x3d, 0x50};
    run_match_finder_case(dictionary, (const unsigned char *)"ABCDEFGHIJKLMNOP", 16, expected, sizeof(expected));
}
