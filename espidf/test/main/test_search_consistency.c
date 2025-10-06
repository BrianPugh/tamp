#include <stdio.h>
#include <string.h>

#include "tamp/common.h"
#include "tamp/compressor.h"
#include "unity.h"

// Reference implementation from compressor.c for comparison
static inline void find_best_match_reference(TampCompressor *compressor, uint16_t *match_index, uint8_t *match_size) {
#define MAX_PATTERN_SIZE_REF (compressor->min_pattern_size + 13)
#define WINDOW_SIZE_REF (1 << compressor->conf_window)
#define input_add(offset) ((compressor->input_pos + offset) & 0xF)
#define read_input(offset) (compressor->input[input_add(offset)])

    *match_size = 0;

    if (TAMP_UNLIKELY(compressor->input_size < compressor->min_pattern_size)) return;

    const uint16_t first_second = (read_input(0) << 8) | read_input(1);
    const uint16_t window_size_minus_1 = WINDOW_SIZE_REF - 1;
    const uint8_t max_pattern_size =
        (compressor->input_size < MAX_PATTERN_SIZE_REF) ? compressor->input_size : MAX_PATTERN_SIZE_REF;

    uint16_t window_rolling_2_byte = compressor->window[0];

    for (uint16_t window_index = 0; window_index < window_size_minus_1; window_index++) {
        window_rolling_2_byte <<= 8;
        window_rolling_2_byte |= compressor->window[window_index + 1];
        if (TAMP_LIKELY(window_rolling_2_byte != first_second)) {
            continue;
        }

        // Found 2-byte match, now extend the match
        uint8_t match_len = 2;

        // Extend match byte by byte with optimized bounds checking
        for (uint8_t i = 2; i < max_pattern_size; i++) {
            if (TAMP_UNLIKELY((window_index + i) > window_size_minus_1)) break;

            if (TAMP_LIKELY(compressor->window[window_index + i] != read_input(i))) break;
            match_len = i + 1;
        }

        // Update best match if this is better
        if (TAMP_UNLIKELY(match_len > *match_size)) {
            *match_size = match_len;
            *match_index = window_index;
            // Early termination if we found the maximum possible match
            if (TAMP_UNLIKELY(*match_size == max_pattern_size)) return;
        }
    }

#undef MAX_PATTERN_SIZE_REF
#undef WINDOW_SIZE_REF
#undef input_add
#undef read_input
}

// External declaration of the ESP32-optimized version
extern void find_best_match(TampCompressor *compressor, uint16_t *match_index, uint8_t *match_size);

// Helper to set up compressor state for testing
static void setup_compressor_state(TampCompressor *compressor, unsigned char *window, const unsigned char *window_data,
                                   size_t window_len, const unsigned char *input_data, size_t input_len) {
    TampConf conf = {
        .window = 10,
        .literal = 8,
        .use_custom_dictionary = false,
    };

    tamp_compressor_init(compressor, &conf, window);

    // Manually set up window with test data
    if (window_data && window_len > 0) {
        memcpy(window, window_data, window_len);
    }

    // Manually set up input buffer
    if (input_data && input_len > 0) {
        compressor->input_size = input_len < sizeof(compressor->input) ? input_len : sizeof(compressor->input);
        memcpy(compressor->input, input_data, compressor->input_size);
        compressor->input_pos = 0;
    }
}

void test_search_simple_match(void) {
    TampCompressor compressor;
    unsigned char window[1 << 10];

    // Set up window with a pattern
    const char *window_data = "abcdefgh";
    const char *input_data = "abcd";  // Should match at position 0

    setup_compressor_state(&compressor, window, (unsigned char *)window_data, strlen(window_data),
                           (unsigned char *)input_data, strlen(input_data));

    uint16_t ref_index = 0, opt_index = 0;
    uint8_t ref_size = 0, opt_size = 0;

    find_best_match_reference(&compressor, &ref_index, &ref_size);

    // Reset state before second call
    setup_compressor_state(&compressor, window, (unsigned char *)window_data, strlen(window_data),
                           (unsigned char *)input_data, strlen(input_data));

    find_best_match(&compressor, &opt_index, &opt_size);

    printf("Simple match: ref_size=%d ref_index=%d, opt_size=%d opt_index=%d\n", ref_size, ref_index, opt_size,
           opt_index);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ref_size, opt_size, "Match size mismatch");
    if (ref_size > 0) {
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(ref_index, opt_index, "Match index mismatch");
    }
}

void test_search_no_match(void) {
    TampCompressor compressor;
    unsigned char window[1 << 10];

    const char *window_data = "abcdefgh";
    const char *input_data = "xyz1";  // Should not match

    setup_compressor_state(&compressor, window, (unsigned char *)window_data, strlen(window_data),
                           (unsigned char *)input_data, strlen(input_data));

    uint16_t ref_index = 0, opt_index = 0;
    uint8_t ref_size = 0, opt_size = 0;

    find_best_match_reference(&compressor, &ref_index, &ref_size);

    setup_compressor_state(&compressor, window, (unsigned char *)window_data, strlen(window_data),
                           (unsigned char *)input_data, strlen(input_data));

    find_best_match(&compressor, &opt_index, &opt_size);

    printf("No match: ref_size=%d, opt_size=%d\n", ref_size, opt_size);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ref_size, opt_size, "Match size should be 0 for both");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, opt_size, "Optimized should find no match");
}

void test_search_multiple_matches(void) {
    TampCompressor compressor;
    unsigned char window[1 << 10];

    // Window with multiple occurrences of pattern
    const char *window_data = "foobarfoobazfoobar";
    const char *input_data = "foobar";  // Should match, longest at position 12

    setup_compressor_state(&compressor, window, (unsigned char *)window_data, strlen(window_data),
                           (unsigned char *)input_data, strlen(input_data));

    uint16_t ref_index = 0, opt_index = 0;
    uint8_t ref_size = 0, opt_size = 0;

    find_best_match_reference(&compressor, &ref_index, &ref_size);

    setup_compressor_state(&compressor, window, (unsigned char *)window_data, strlen(window_data),
                           (unsigned char *)input_data, strlen(input_data));

    find_best_match(&compressor, &opt_index, &opt_size);

    printf("Multiple matches: ref_size=%d ref_index=%d, opt_size=%d opt_index=%d\n", ref_size, ref_index, opt_size,
           opt_index);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ref_size, opt_size, "Match size mismatch");
    if (ref_size > 0) {
        // Both should find the same best match (last occurrence is typically preferred)
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(ref_index, opt_index, "Match index mismatch");
    }
}

void test_search_partial_match(void) {
    TampCompressor compressor;
    unsigned char window[1 << 10];

    // Window with partial matches
    const char *window_data = "abcdef";
    const char *input_data = "abcxyz";  // Matches "abc" only

    setup_compressor_state(&compressor, window, (unsigned char *)window_data, strlen(window_data),
                           (unsigned char *)input_data, strlen(input_data));

    uint16_t ref_index = 0, opt_index = 0;
    uint8_t ref_size = 0, opt_size = 0;

    find_best_match_reference(&compressor, &ref_index, &ref_size);

    setup_compressor_state(&compressor, window, (unsigned char *)window_data, strlen(window_data),
                           (unsigned char *)input_data, strlen(input_data));

    find_best_match(&compressor, &opt_index, &opt_size);

    printf("Partial match: ref_size=%d ref_index=%d, opt_size=%d opt_index=%d\n", ref_size, ref_index, opt_size,
           opt_index);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ref_size, opt_size, "Match size mismatch");
    if (ref_size > 0) {
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(ref_index, opt_index, "Match index mismatch");
    }
}

void test_search_long_pattern(void) {
    TampCompressor compressor;
    unsigned char window[1 << 10];

    // Test with a longer repeating pattern
    const char *window_data = "0123456789abcdef0123456789abcdef";
    const char *input_data = "0123456789ab";  // 12-byte match

    setup_compressor_state(&compressor, window, (unsigned char *)window_data, strlen(window_data),
                           (unsigned char *)input_data, strlen(input_data));

    uint16_t ref_index = 0, opt_index = 0;
    uint8_t ref_size = 0, opt_size = 0;

    find_best_match_reference(&compressor, &ref_index, &ref_size);

    setup_compressor_state(&compressor, window, (unsigned char *)window_data, strlen(window_data),
                           (unsigned char *)input_data, strlen(input_data));

    find_best_match(&compressor, &opt_index, &opt_size);

    printf("Long pattern: ref_size=%d ref_index=%d, opt_size=%d opt_index=%d\n", ref_size, ref_index, opt_size,
           opt_index);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ref_size, opt_size, "Match size mismatch");
    if (ref_size > 0) {
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(ref_index, opt_index, "Match index mismatch");
    }
}

void test_search_edge_case_minimum_pattern(void) {
    TampCompressor compressor;
    unsigned char window[1 << 10];

    // Test with exactly min_pattern_size (typically 3 bytes for window=10, literal=8)
    const char *window_data = "abcdefgh";
    const char *input_data = "abc";  // Exactly minimum size

    setup_compressor_state(&compressor, window, (unsigned char *)window_data, strlen(window_data),
                           (unsigned char *)input_data, strlen(input_data));

    uint16_t ref_index = 0, opt_index = 0;
    uint8_t ref_size = 0, opt_size = 0;

    find_best_match_reference(&compressor, &ref_index, &ref_size);

    setup_compressor_state(&compressor, window, (unsigned char *)window_data, strlen(window_data),
                           (unsigned char *)input_data, strlen(input_data));

    find_best_match(&compressor, &opt_index, &opt_size);

    printf("Min pattern: ref_size=%d ref_index=%d, opt_size=%d opt_index=%d\n", ref_size, ref_index, opt_size,
           opt_index);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ref_size, opt_size, "Match size mismatch");
    if (ref_size > 0) {
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(ref_index, opt_index, "Match index mismatch");
    }
}

void test_search_binary_data(void) {
    TampCompressor compressor;
    unsigned char window[1 << 10];

    // Test with binary data including null bytes
    unsigned char window_data[] = {0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0xFF};
    unsigned char input_data[] = {0x00, 0x01, 0x02, 0x03};

    setup_compressor_state(&compressor, window, window_data, sizeof(window_data), input_data, sizeof(input_data));

    uint16_t ref_index = 0, opt_index = 0;
    uint8_t ref_size = 0, opt_size = 0;

    find_best_match_reference(&compressor, &ref_index, &ref_size);

    setup_compressor_state(&compressor, window, window_data, sizeof(window_data), input_data, sizeof(input_data));

    find_best_match(&compressor, &opt_index, &opt_size);

    printf("Binary data: ref_size=%d ref_index=%d, opt_size=%d opt_index=%d\n", ref_size, ref_index, opt_size,
           opt_index);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ref_size, opt_size, "Match size mismatch");
    if (ref_size > 0) {
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(ref_index, opt_index, "Match index mismatch");
    }
}

void test_search_window_boundary(void) {
    TampCompressor compressor;
    unsigned char window[1 << 10];
    TampConf conf = {
        .window = 10,
        .literal = 8,
        .use_custom_dictionary = true,  // Don't overwrite our test pattern
    };

    // Fill window and test pattern near the end (but not past the boundary)
    memset(window, 'x', sizeof(window));
    // Copy only 10 bytes to avoid buffer overflow (1024-10=1014, plus 10 bytes = 1023, which is the last valid index)
    memcpy(window + sizeof(window) - 10, "testpatter", 10);  // "testpatter" without the 'n'

    unsigned char input_data[] = "testpat";  // 7 bytes

    tamp_compressor_init(&compressor, &conf, window);

    // Manually set up input buffer
    compressor.input_size = sizeof(input_data) - 1;  // -1 for null terminator
    memcpy(compressor.input, input_data, compressor.input_size);
    compressor.input_pos = 0;

    uint16_t ref_index = 0, opt_index = 0;
    uint8_t ref_size = 0, opt_size = 0;

    find_best_match_reference(&compressor, &ref_index, &ref_size);

    // Reset input buffer for second call
    compressor.input_size = sizeof(input_data) - 1;
    memcpy(compressor.input, input_data, compressor.input_size);
    compressor.input_pos = 0;

    find_best_match(&compressor, &opt_index, &opt_size);

    printf("Window boundary: ref_size=%d ref_index=%d, opt_size=%d opt_index=%d\n", ref_size, ref_index, opt_size,
           opt_index);

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(ref_size, opt_size, "Match size mismatch");
    if (ref_size > 0) {
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(ref_index, opt_index, "Match index mismatch");
    }
}
