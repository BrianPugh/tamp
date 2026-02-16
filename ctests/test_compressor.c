#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "tamp/compressor.h"
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
