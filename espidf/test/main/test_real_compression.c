#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "tamp/common.h"
#include "tamp/compressor.h"
#include "unity.h"

// This test compresses real text data and compares the output
// If the ESP32 optimized search differs from reference, we should see different compressed output

void test_real_text_compression(void) {
    // Use a realistic text sample that might appear in enwik8
    const char *input =
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy cat. "
        "The quick brown fox jumps over the lazy fox. "
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ "
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ "
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ "
        "0123456789 0123456789 0123456789 "
        "The end of the test string with repeated patterns. "
        "The end of the test string with repeated patterns. ";

    TampCompressor compressor;
    unsigned char window[1 << 10];
    unsigned char output[1024];
    size_t output_len;
    size_t consumed;

    TampConf conf = {
        .window = 10,
        .literal = 8,
        .use_custom_dictionary = false,
    };

    tamp_res res = tamp_compressor_init(&compressor, &conf, window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    res = tamp_compressor_compress_and_flush(&compressor, output, sizeof(output), &output_len, (unsigned char *)input,
                                             strlen(input), &consumed, false);

    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(strlen(input), consumed);

    printf("Real text compression: input=%zu bytes, output=%zu bytes, ratio=%.2f%%\n", strlen(input), output_len,
           (100.0 * output_len) / strlen(input));

    // Print first 32 bytes of compressed output as hex
    printf("Compressed bytes (first 32): ");
    for (size_t i = 0; i < 32 && i < output_len; i++) {
        printf("%02x ", output[i]);
    }
    printf("\n");

    // The compressed size should be significantly smaller than input
    TEST_ASSERT_LESS_THAN(strlen(input), output_len);

    // NOTE: We can't compare against a reference implementation here in the same test
    // But we can document what the expected output should be
    // If the search algorithm changes, this compressed output will change too
}

void test_enwik8_like_text(void) {
    // Text pattern similar to what you'd find in enwik8 (Wikipedia XML)
    const char *input =
        "<page><title>Article</title><text>This is the first article. "
        "This is the first article with more text. "
        "This is the first article with more text and repetition. "
        "</text></page>"
        "<page><title>Article</title><text>This is the second article. "
        "This is the second article with more text. "
        "This is the second article with more text and repetition. "
        "</text></page>"
        "<page><title>Article</title><text>This is the third article. "
        "This is the third article with more text. "
        "This is the third article with more text and repetition. "
        "</text></page>";

    TampCompressor compressor;
    unsigned char window[1 << 10];
    unsigned char output[1024];
    size_t output_len;
    size_t consumed;

    TampConf conf = {
        .window = 10,
        .literal = 8,
        .use_custom_dictionary = false,
    };

    tamp_res res = tamp_compressor_init(&compressor, &conf, window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    res = tamp_compressor_compress_and_flush(&compressor, output, sizeof(output), &output_len, (unsigned char *)input,
                                             strlen(input), &consumed, false);

    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(strlen(input), consumed);

    printf("Enwik8-like text: input=%zu bytes, output=%zu bytes, ratio=%.2f%%\n", strlen(input), output_len,
           (100.0 * output_len) / strlen(input));

    printf("Compressed output checksum: ");
    // Simple checksum of output
    uint32_t checksum = 0;
    for (size_t i = 0; i < output_len; i++) {
        checksum = ((checksum << 5) + checksum) + output[i];
    }
    printf("0x%08" PRIx32 "\n", checksum);

    // Document this checksum - if the algorithm changes, it will change too
    // On ESP32 reference: checksum should be consistent
    // On ESP32-S3 optimized: if bug exists, checksum will differ
}
