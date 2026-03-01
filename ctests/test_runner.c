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
    RUN_TEST(test_decompress_cb_callback_receives_input_consumed);

    // Compressor tests
    RUN_TEST(test_compressor_init);
    RUN_TEST(test_compressor_simple);
    RUN_TEST(test_compressor_extended_simple);
    RUN_TEST(test_compressor_extended_rle_roundtrip);
    RUN_TEST(test_compressor_extended_match_roundtrip);
    RUN_TEST(test_compressor_extended_rle_transition_roundtrip);
    RUN_TEST(test_compressor_extended_window8_roundtrip);
    RUN_TEST(test_compressor_extended_window9_roundtrip);
#if TAMP_LAZY_MATCHING
    RUN_TEST(test_compressor_extended_lazy_roundtrip);
#endif
    RUN_TEST(test_compress_cb_callback_receives_input_consumed);
    RUN_TEST(test_compress_cb_callback_abort);

    // Decompressor extended tests
    RUN_TEST(test_decompressor_extended_rle);
    RUN_TEST(test_decompressor_extended_match);

    // Stream tests
    RUN_TEST(test_decompress_stream_simple);
    RUN_TEST(test_stream_roundtrip);
    RUN_TEST(test_stream_extended_roundtrip);
    RUN_TEST(test_stream_extended_rle_roundtrip);
    RUN_TEST(test_compress_stream_callback_receives_input_consumed);
    RUN_TEST(test_decompress_stream_callback_receives_input_consumed);

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
