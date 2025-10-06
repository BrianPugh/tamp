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
