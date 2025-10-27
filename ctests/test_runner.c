#include "unity.h"

// Unity requires these functions
void setUp(void) { /* This is run before EACH test */
}

void tearDown(void) { /* This is run after EACH test */
}

// Include test files directly
#include "test_compressor.c"
#include "test_decompressor.c"

int main(void) {
    UNITY_BEGIN();

    // Decompressor tests
    RUN_TEST(test_decompressor_byte_by_byte);
    RUN_TEST(test_decompressor_malicious_oob);

    // Compressor tests
    RUN_TEST(test_compressor_init);
    RUN_TEST(test_compressor_simple);

    return UNITY_END();
}
