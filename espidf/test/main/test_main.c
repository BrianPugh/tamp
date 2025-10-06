#include <stdio.h>
#include <string.h>

#include "esp_system.h"
#include "unity.h"

// Test function declarations
extern void test_esp32_compressor_init(void);
extern void test_esp32_compressor_simple(void);
extern void test_esp32_compressor_repeated_pattern(void);
extern void test_esp32_decompressor_byte_by_byte(void);
extern void test_esp32_decompressor_simple(void);

// Search consistency tests
extern void test_search_simple_match(void);
extern void test_search_no_match(void);
extern void test_search_multiple_matches(void);
extern void test_search_partial_match(void);
extern void test_search_long_pattern(void);
extern void test_search_edge_case_minimum_pattern(void);
extern void test_search_binary_data(void);
extern void test_search_window_boundary(void);

void setUp(void) {
    // This is run before EACH test
}

void tearDown(void) {
    // This is run after EACH test
}

void app_main(void) {
    printf("\n");
    printf("========================================\n");
    printf("  TAMP ESP32 QEMU Test Suite\n");
    printf("========================================\n");
    printf("\n");

    UNITY_BEGIN();

    // Compressor tests
    printf("\n--- Compressor Tests ---\n");
    RUN_TEST(test_esp32_compressor_init);
    RUN_TEST(test_esp32_compressor_simple);
    RUN_TEST(test_esp32_compressor_repeated_pattern);

    // Decompressor tests
    printf("\n--- Decompressor Tests ---\n");
    RUN_TEST(test_esp32_decompressor_byte_by_byte);
    RUN_TEST(test_esp32_decompressor_simple);

    // Search consistency tests (comparing reference vs optimized implementation)
    printf("\n--- Search Consistency Tests ---\n");
    RUN_TEST(test_search_simple_match);
    RUN_TEST(test_search_no_match);
    RUN_TEST(test_search_multiple_matches);
    RUN_TEST(test_search_partial_match);
    RUN_TEST(test_search_long_pattern);
    RUN_TEST(test_search_edge_case_minimum_pattern);
    RUN_TEST(test_search_binary_data);
    RUN_TEST(test_search_window_boundary);

    int result = UNITY_END();

    printf("\n");
    printf("========================================\n");
    if (result == 0) {
        printf("  ALL TESTS PASSED\n");
    } else {
        printf("  SOME TESTS FAILED\n");
    }
    printf("========================================\n");
    printf("\n");

    // Exit for QEMU
    printf("Test complete. Exiting...\n");
    fflush(stdout);
}
