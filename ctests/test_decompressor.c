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

void test_decompressor_extended_rle(void) {
    /*****
     * Tests decompression of extended-format data with an RLE token.
     * Compressed data encodes 20 bytes of 'A' (literal 'A' + RLE count=19).
     */
    const unsigned char compressed[] = {
        0x5A,  // header: window=10, literal=8, extended=1
        0xA0,  // literal 'A' + start of RLE token
        0xAA,  // RLE huffman continued
        0xB1,  // count encoding + padding
    };

    tamp_res res;
    TampDecompressor d;
    unsigned char window_buffer[1 << 10];

    res = tamp_decompressor_init(&d, NULL, window_buffer, 10);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    unsigned char output[32];
    size_t output_written;

    res =
        tamp_decompressor_decompress(&d, output, sizeof(output), &output_written, compressed, sizeof(compressed), NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(20, output_written);

    // Verify all bytes are 'A'
    unsigned char expected[20];
    memset(expected, 'A', sizeof(expected));
    TEST_ASSERT_EQUAL_MEMORY(expected, output, 20);
}

void test_decompressor_extended_match(void) {
    /*****
     * Tests decompression of extended-format data with an extended match token.
     * Compressed data: 14-byte match at index 0 using custom dictionary.
     * conf=NULL so header is read from the compressed data.
     */
    const unsigned char compressed[] = {
        0x1E,  // header: window=8, literal=8, custom=1, extended=1
        0x4E,  // ext_match token + count encoding
        0x00,  // position + padding
        0x00,  // padding
    };

    // Pre-initialize the window buffer with the dictionary
    unsigned char window[1 << 8];
    memset(window, 0, sizeof(window));
    const char *pattern = "abcdefghijklmn";
    memcpy(window, pattern, 14);

    tamp_res res;
    TampDecompressor d;

    // conf=NULL: header is read from compressed data during decompress.
    // Since header has custom_dict=1, the window is preserved (not re-initialized).
    res = tamp_decompressor_init(&d, NULL, window, 8);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    unsigned char output[32];
    size_t output_written;

    res =
        tamp_decompressor_decompress(&d, output, sizeof(output), &output_written, compressed, sizeof(compressed), NULL);
    TEST_ASSERT_GREATER_OR_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(14, output_written);
    TEST_ASSERT_EQUAL_MEMORY(pattern, output, 14);
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

static int count_callback(void *user_data, size_t bytes_processed, size_t total_bytes) {
    (void)bytes_processed;
    (void)total_bytes;
    (*(int *)user_data)++;
    return 0;
}

void test_decompressor_repeated_flush_resets_once(void) {
    /*****
     * Regression: every FLUSH after a double-FLUSH re-initialized the whole window, so a
     * stream of 2-byte FLUSH tokens forced a full dictionary init per 2 input bytes without
     * ever invoking the callback. A reset window is pristine, so only the first double-FLUSH
     * since the last literal/match may reset it.
     */
    const unsigned char header[] = {0x1B, 0x00};                 // window=8, literal=8, extended, dictionary_reset
    const unsigned char flush[] = {0x55, 0x80};                  // FLUSH token, byte-aligned
    const unsigned char literal_a_flush[] = {0xA0, 0xAA, 0xC0};  // literal 'A' + FLUSH

    TampDecompressor d;
    unsigned char window[1 << 8];
    unsigned char output[8];
    size_t written, consumed;
    int calls = 0;

    TEST_ASSERT_EQUAL(TAMP_OK, tamp_decompressor_init(&d, NULL, window, 8));
    TEST_ASSERT_EQUAL(TAMP_INPUT_EXHAUSTED,
                      tamp_decompressor_decompress(&d, output, sizeof(output), &written, header, sizeof(header), NULL));
    for (int i = 0; i < 2; i++)  // double-FLUSH: resets
        TEST_ASSERT_EQUAL(TAMP_INPUT_EXHAUSTED, tamp_decompressor_decompress(&d, output, sizeof(output), &written,
                                                                             flush, sizeof(flush), NULL));
    const unsigned char dictionary_byte = window[0];

    // Further FLUSHes must not re-initialize the window (sentinel survives), and must still
    // invoke the progress callback.
    window[0] = (unsigned char)~dictionary_byte;
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(TAMP_INPUT_EXHAUSTED,
                          tamp_decompressor_decompress_cb(&d, output, sizeof(output), &written, flush, sizeof(flush),
                                                          &consumed, count_callback, &calls));
        TEST_ASSERT_EQUAL(sizeof(flush), consumed);
        TEST_ASSERT_EQUAL(0, written);
    }
    TEST_ASSERT_EQUAL(3, calls);
    TEST_ASSERT_EQUAL((unsigned char)~dictionary_byte, window[0]);

    // After data, a double-FLUSH resets again.
    TEST_ASSERT_EQUAL(TAMP_INPUT_EXHAUSTED,
                      tamp_decompressor_decompress(&d, output, sizeof(output), &written, literal_a_flush,
                                                   sizeof(literal_a_flush), NULL));
    TEST_ASSERT_EQUAL(1, written);
    TEST_ASSERT_EQUAL('A', output[0]);
    TEST_ASSERT_EQUAL(TAMP_INPUT_EXHAUSTED,
                      tamp_decompressor_decompress(&d, output, sizeof(output), &written, flush, sizeof(flush), NULL));
    TEST_ASSERT_EQUAL(dictionary_byte, window[0]);
}

static int always_abort_callback(void *user_data, size_t bytes_processed, size_t total_bytes) {
    (void)bytes_processed;
    (void)total_bytes;
    (*(int *)user_data)++;
    return 1;
}

void test_decompressor_callback_abort_reports_progress(void) {
    /*****
     * Regression: callbacks after FLUSH and extended tokens returned before the consumed/written
     * counts were stored, so a caller resuming after an abort used stale counts. Abort at every
     * callback and resume from the reported counts; the output must still be exact.
     */
    TampConf conf = {.window = 8, .literal = 8, .extended = 1, .dictionary_reset = 1};
    unsigned char c_window[1 << 8];
    unsigned char compressed[256];
    size_t compressed_size = 0, n;
    TampCompressor c;
    TEST_ASSERT_EQUAL(TAMP_OK, tamp_compressor_init(&c, &conf, c_window));

    unsigned char original[160];
    memset(original, 'A', 40);                                                               // RLE
    for (int i = 40; i < 120; i++) original[i] = (unsigned char)("tamp-callback-"[i % 14]);  // extended match
    memset(original + 120, 'B', 40);

#define COMPRESS(start, len)                                                                                          \
    TEST_ASSERT_EQUAL(                                                                                                \
        TAMP_OK, tamp_compressor_compress(&c, compressed + compressed_size, sizeof(compressed) - compressed_size, &n, \
                                          original + (start), (len), NULL));                                          \
    compressed_size += n
    COMPRESS(0, 40);
    TEST_ASSERT_EQUAL(TAMP_OK, tamp_compressor_flush(&c, compressed + compressed_size,
                                                     sizeof(compressed) - compressed_size, &n, true));
    compressed_size += n;
    COMPRESS(40, 80);
    TEST_ASSERT_EQUAL(TAMP_OK, tamp_compressor_reset_dictionary(&c, compressed + compressed_size,
                                                                sizeof(compressed) - compressed_size, &n));
    compressed_size += n;
    COMPRESS(120, 40);
#undef COMPRESS
    TEST_ASSERT_EQUAL(TAMP_OK, tamp_compressor_flush(&c, compressed + compressed_size,
                                                     sizeof(compressed) - compressed_size, &n, false));
    compressed_size += n;

    TampDecompressor d;
    unsigned char d_window[1 << 8];
    unsigned char output[sizeof(original) + 8];
    size_t in_pos = 0, out_pos = 0;
    int calls = 0;
    TEST_ASSERT_EQUAL(TAMP_OK, tamp_decompressor_init(&d, NULL, d_window, 8));
    for (int iter = 0; iter < 1000 && in_pos < compressed_size; iter++) {
        size_t consumed = (size_t)-1, written = (size_t)-1;
        tamp_res res = tamp_decompressor_decompress_cb(&d, output + out_pos, sizeof(output) - out_pos, &written,
                                                       compressed + in_pos, compressed_size - in_pos, &consumed,
                                                       always_abort_callback, &calls);
        TEST_ASSERT_TRUE(res == 1 || res == TAMP_INPUT_EXHAUSTED);
        TEST_ASSERT_LESS_OR_EQUAL(compressed_size - in_pos, consumed);
        TEST_ASSERT_LESS_OR_EQUAL(sizeof(output) - out_pos, written);
        in_pos += consumed;
        out_pos += written;
    }
    // Drain tokens still buffered in the bit buffer.
    for (int iter = 0; iter < 1000; iter++) {
        size_t written = 0;
        tamp_res res = tamp_decompressor_decompress_cb(&d, output + out_pos, sizeof(output) - out_pos, &written, NULL,
                                                       0, NULL, always_abort_callback, &calls);
        out_pos += written;
        if (res == TAMP_INPUT_EXHAUSTED) break;
    }
    TEST_ASSERT_GREATER_THAN(10, calls);
    TEST_ASSERT_EQUAL(sizeof(original), out_pos);
    TEST_ASSERT_EQUAL_MEMORY(original, output, sizeof(original));
}
