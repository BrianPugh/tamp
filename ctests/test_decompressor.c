#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "tamp/compressor.h"
#include "tamp/decompressor.h"
#include "unity.h"

void test_decompressor_byte_by_byte(void) {
    /*****
     * Tests the decompressor if we feed in 1 byte at a time.
     */
    const unsigned char compressed[] = {
        0b01011000,  // header (window_bits=10, literal_bits=8)
        0b10110011,  // literal "f"
        0b00000100,  // the pre-init buffer contains "oo" at index 131
                     // size=2 -> 0b0
                     // 131 -> 0b0010000011
        0b00011100,  // literal " "
        0b10000001,  // There is now "foo " at index 0
        0b00000000,  // size=4 -> 0b1000
        0b00000011,  // Just "foo" at index 0; size=3 -> 0b11
        0b00000000,  // index 0 -> 0b0000000000
        0b00000000,  // 6 bits of zero-padding
    };

    tamp_res res;
    TampDecompressor d;
    unsigned char window_buffer[1 << 10];

    res = tamp_decompressor_init(&d, NULL, window_buffer, 10);
    TEST_ASSERT_EQUAL(res, TAMP_OK);

    unsigned char input_byte;
    size_t read_size = 0;
    size_t input_chunk_consumed_size;
    bool file_exhausted = false;

    size_t output_written_size = 0;

    unsigned char output_buffer_complete[32] = {0};
    unsigned char *output_buffer = output_buffer_complete;

    size_t output_chunk_written_size;

    int j = 0;
    while (true) {
        res = tamp_decompressor_decompress(&d, output_buffer, sizeof(output_buffer), &output_chunk_written_size,
                                           &input_byte, read_size, &input_chunk_consumed_size);
        assert(res >= TAMP_OK);  // i.e. an "ok" result
        assert(input_chunk_consumed_size == read_size);

        output_written_size += output_chunk_written_size;
        output_buffer += output_chunk_written_size;

        if (res == TAMP_OUTPUT_FULL) break;
        if (res == TAMP_INPUT_EXHAUSTED && file_exhausted) break;

        if (j >= sizeof(compressed)) {
            read_size = 0;
        } else {
            input_byte = compressed[j];
            j++;
            read_size = 1;
        }

        file_exhausted = read_size == 0;
    }

    TEST_ASSERT_EQUAL_STRING("foo foo foo", output_buffer_complete);
}

void test_decompressor_malicious_oob(void) {
    /*****
     * Tests the decompressor if we feed in a pattern at position WINDOW_SIZE - 1.
     * The compressor should never generate this.
     */
    const unsigned char compressed[] = {
        0b01011000,  // header (window_bits=10, literal_bits=8)
        0b00111111,  // pattern of length 2 at WINDOW_SIZE -1
        0b11110000,  // 4 bits of zero-padding
    };

    tamp_res res;
    TampDecompressor d;
    unsigned char window_buffer[1 << 10];

    res = tamp_decompressor_init(&d, NULL, window_buffer, 10);
    TEST_ASSERT_EQUAL(res, TAMP_OK);

    unsigned char output_buffer[32];
    size_t output_written_size;

    res = tamp_decompressor_decompress(&d, output_buffer, sizeof(output_buffer), &output_written_size, compressed,
                                       sizeof(compressed), NULL);
    TEST_ASSERT_EQUAL(res, TAMP_OOB);
}

void test_decompress_cb_callback_receives_input_consumed(void) {
    // First, compress some data to get valid compressed input
    TampCompressor compressor;
    unsigned char window[1 << 10];
    unsigned char compressed[256];
    size_t compressed_size, input_consumed;
    TampConf conf = {.window = 10, .literal = 8};

    tamp_compressor_init(&compressor, &conf, window);

    const char *original =
        "The quick brown fox jumps over the lazy dog. "
        "The quick brown fox jumps over the lazy dog.";
    size_t original_len = strlen(original);

    tamp_compressor_compress_and_flush(&compressor, compressed, sizeof(compressed), &compressed_size,
                                       (const unsigned char *)original, original_len, &input_consumed, false);

    // Now decompress with callback tracking
    // Skip the header byte for total_bytes calculation
    TampDecompressor decompressor;
    tamp_decompressor_init(&decompressor, NULL, window, 10);

    unsigned char output[256];
    size_t output_written;
    size_t decompress_input_consumed;

    CallbackTracker tracker;
    callback_tracker_init(&tracker, compressed_size);

    tamp_res res =
        tamp_decompressor_decompress_cb(&decompressor, output, sizeof(output), &output_written, compressed,
                                        compressed_size, &decompress_input_consumed, tracking_callback, &tracker);

    // Callback should have been called
    TEST_ASSERT_GREATER_THAN(0, tracker.call_count);

    // Monotonicity and total_bytes consistency are checked in the callback itself.
    // Final bytes_processed should equal input_consumed
    TEST_ASSERT_EQUAL(decompress_input_consumed, tracker.last_bytes_processed);

    // Verify decompression worked
    TEST_ASSERT_EQUAL(original_len, output_written);
    TEST_ASSERT_EQUAL_MEMORY(original, output, original_len);
}
