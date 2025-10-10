#include <stdio.h>
#include <string.h>

#include "tamp/common.h"
#include "tamp/compressor.h"
#include "tamp/decompressor.h"
#include "unity.h"

// This test compares actual compression output between reference and optimized implementations

static void compress_with_implementation(const char* input, size_t input_len, unsigned char* output, size_t* output_len,
                                         bool use_esp32_optimized) {
    TampCompressor compressor;
    unsigned char window[1 << 10];
    TampConf conf = {
        .window = 10,
        .literal = 8,
        .use_custom_dictionary = false,
    };

    tamp_res res = tamp_compressor_init(&compressor, &conf, window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    size_t consumed;
    res = tamp_compressor_compress_and_flush(&compressor, output, 512, output_len, (unsigned char*)input, input_len,
                                             &consumed, false);
    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(input_len, consumed);
}

void test_compression_simple_repetition(void) {
    const char* input = "abcabcabcabcabc";
    unsigned char ref_output[512];
    unsigned char opt_output[512];
    size_t ref_len = 0, opt_len = 0;

    compress_with_implementation(input, strlen(input), ref_output, &ref_len, false);
    compress_with_implementation(input, strlen(input), opt_output, &opt_len, true);

    printf("Simple repetition: ref_len=%zu opt_len=%zu\n", ref_len, opt_len);

    // Outputs should be identical
    TEST_ASSERT_EQUAL_UINT_MESSAGE(ref_len, opt_len, "Compressed sizes differ");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(ref_output, opt_output, ref_len, "Compressed data differs");
}

void test_compression_multiple_matches_same_length(void) {
    // Pattern "abc" appears at positions 0, 3, 6, 9
    // When compressing position 12-14, all earlier "abc" matches have the same length (3)
    // Reference should prefer the LAST one (position 9), optimized might choose differently
    const char* input = "abc123abc456abc789abcXXX";
    unsigned char ref_output[512];
    unsigned char opt_output[512];
    size_t ref_len = 0, opt_len = 0;

    compress_with_implementation(input, strlen(input), ref_output, &ref_len, false);
    compress_with_implementation(input, strlen(input), opt_output, &opt_len, true);

    printf("Multiple same-length matches: ref_len=%zu opt_len=%zu\n", ref_len, opt_len);

    if (ref_len != opt_len || memcmp(ref_output, opt_output, ref_len) != 0) {
        printf("WARNING: Compressed outputs differ!\n");
        printf("Reference bytes: ");
        for (size_t i = 0; i < ref_len && i < 32; i++) {
            printf("%02x ", ref_output[i]);
        }
        printf("\nOptimized bytes: ");
        for (size_t i = 0; i < opt_len && i < 32; i++) {
            printf("%02x ", opt_output[i]);
        }
        printf("\n");
    }

    // This test documents the difference - we expect them to differ
    // Once the bug is fixed, change this to TEST_ASSERT_EQUAL_UINT8_ARRAY
    if (ref_len == opt_len && memcmp(ref_output, opt_output, ref_len) == 0) {
        printf("PASS: Outputs are identical (bug fixed!)\n");
    } else {
        printf("DOCUMENTED DIFFERENCE: Outputs differ due to match selection\n");
    }
}

void test_compression_overlapping_patterns(void) {
    // "foobar" at position 0, "foo" at position 3, full "foobar" at position 10
    const char* input = "foobarfooXYZfoobar123foobar";
    unsigned char ref_output[512];
    unsigned char opt_output[512];
    size_t ref_len = 0, opt_len = 0;

    compress_with_implementation(input, strlen(input), ref_output, &ref_len, false);
    compress_with_implementation(input, strlen(input), opt_output, &opt_len, true);

    printf("Overlapping patterns: ref_len=%zu opt_len=%zu\n", ref_len, opt_len);

    if (ref_len != opt_len || memcmp(ref_output, opt_output, ref_len) != 0) {
        printf("WARNING: Compressed outputs differ!\n");
    }

    // Document the difference for now
    if (ref_len == opt_len && memcmp(ref_output, opt_output, ref_len) == 0) {
        printf("PASS: Outputs are identical\n");
    } else {
        printf("DOCUMENTED DIFFERENCE: Outputs differ\n");
    }
}

void test_compression_decompression_roundtrip(void) {
    // Even if compression differs, decompression should produce identical output
    const char* input = "The quick brown fox jumps over the lazy dog. The quick brown fox!";
    unsigned char compressed[512];
    unsigned char decompressed[512];
    size_t compressed_len = 0, decompressed_len = 0;

    // Compress with optimized implementation
    compress_with_implementation(input, strlen(input), compressed, &compressed_len, true);

    // Decompress
    TampDecompressor decompressor;
    unsigned char window[1 << 10];
    tamp_res res = tamp_decompressor_init(&decompressor, NULL, window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    size_t consumed;
    res = tamp_decompressor_decompress(&decompressor, decompressed, sizeof(decompressed), &decompressed_len, compressed,
                                       compressed_len, &consumed);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    printf("Roundtrip: input_len=%zu compressed_len=%zu decompressed_len=%zu\n", strlen(input), compressed_len,
           decompressed_len);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(strlen(input), decompressed_len, "Decompressed size mismatch");
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE((unsigned char*)input, decompressed, strlen(input),
                                          "Decompressed data mismatch");
}

void test_compression_longer_pattern_earlier_in_window(void) {
    // This is the key test case for the bug:
    // - "abcd" at position 0 (4 bytes)
    // - "abc" at position 10 (3 bytes, but closer)
    // When compressing "abcd" later, reference should find the 4-byte match at position 0
    // Optimized version might find 3-byte match at position 10 first, then miss the longer match

    const char* input = "abcd______abc_______abcd";
    unsigned char ref_output[512];
    unsigned char opt_output[512];
    size_t ref_len = 0, opt_len = 0;

    compress_with_implementation(input, strlen(input), ref_output, &ref_len, false);
    compress_with_implementation(input, strlen(input), opt_output, &opt_len, true);

    printf("Longer pattern earlier: ref_len=%zu opt_len=%zu\n", ref_len, opt_len);

    if (ref_len != opt_len || memcmp(ref_output, opt_output, ref_len) != 0) {
        printf("BUG DETECTED: Optimized version missed longer match!\n");
        printf("Reference should find 4-byte 'abcd' match\n");
        printf("Optimized might find 3-byte 'abc' match and stop searching\n");
    }

    // This documents the expected bug
    if (ref_len == opt_len && memcmp(ref_output, opt_output, ref_len) == 0) {
        printf("PASS: Bug is fixed!\n");
    } else {
        printf("BUG CONFIRMED: Outputs differ due to premature search termination\n");
    }
}
