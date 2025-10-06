#include <string.h>

#include "tamp/compressor.h"
#include "unity.h"

void test_esp32_compressor_init(void) {
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

void test_esp32_compressor_simple(void) {
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
    TEST_ASSERT_EQUAL(strlen(input), input_consumed);
}

void test_esp32_compressor_repeated_pattern(void) {
    tamp_res res;
    TampCompressor compressor;
    unsigned char window[1 << 10];
    unsigned char output[512];
    size_t output_written;
    size_t input_consumed;
    TampConf conf = {
        .window = 10,
        .literal = 8,
        .use_custom_dictionary = false,
    };

    res = tamp_compressor_init(&compressor, &conf, window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    // Test with repeated pattern that should compress well
    const char *input = "abcabcabcabcabcabcabcabcabcabc";
    res = tamp_compressor_compress_and_flush(&compressor, output, sizeof(output), &output_written,
                                             (unsigned char *)input, strlen(input), &input_consumed, false);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_GREATER_THAN(0, output_written);
    TEST_ASSERT_EQUAL(strlen(input), input_consumed);

    // Compressed size should be significantly smaller than input
    TEST_ASSERT_LESS_THAN(strlen(input), output_written);
}
