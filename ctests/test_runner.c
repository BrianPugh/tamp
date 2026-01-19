#include "unity.h"

// Unity requires these functions
void setUp(void) { /* This is run before EACH test */
}

void tearDown(void) { /* This is run after EACH test */
}

// Include test files directly
#include "test_compressor.c"
#include "test_decompressor.c"
#include "test_stream.c"
#include "test_stream_filesystems.c"

int main(void) {
    UNITY_BEGIN();

    // Decompressor tests
    RUN_TEST(test_decompressor_byte_by_byte);
    RUN_TEST(test_decompressor_malicious_oob);

    // Compressor tests
    RUN_TEST(test_compressor_init);
    RUN_TEST(test_compressor_simple);

    // Stream tests
    RUN_TEST(test_decompress_stream_simple);
    RUN_TEST(test_stream_roundtrip);

    // Built-in I/O handler tests
    RUN_TEST(test_stdio_handlers_roundtrip);

    // Filesystem handler tests
#ifdef TEST_LITTLEFS
    RUN_TEST(test_littlefs_roundtrip);
#endif
#ifdef TEST_FATFS
    RUN_TEST(test_fatfs_roundtrip);
#endif

    return UNITY_END();
}
