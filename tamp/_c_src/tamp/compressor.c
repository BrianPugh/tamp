#include "compressor.h"

#include <stdbool.h>
#include <stdlib.h>

#include "common.h"

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2 * !!(condition)]))

#if TAMP_EXTENDED_COMPRESS
// Extended max pattern: min_pattern_size + 11 + 112 = min_pattern_size + 123
#define MAX_PATTERN_SIZE_EXTENDED (compressor->min_pattern_size + 123)
#define MAX_PATTERN_SIZE (compressor->conf.extended ? MAX_PATTERN_SIZE_EXTENDED : (compressor->min_pattern_size + 13))
#else
#define MAX_PATTERN_SIZE (compressor->min_pattern_size + 13)
#endif
#define WINDOW_SIZE (1 << compressor->conf.window)
// 0xF because sizeof(TampCompressor.input) == 16;
#define input_add(offset) ((compressor->input_pos + offset) & 0xF)
#define read_input(offset) (compressor->input[input_add(offset)])
#define IS_LITERAL_FLAG (1 << compressor->conf.literal)

#define FLUSH_CODE (0xAB)

// encodes [min_pattern_bytes, min_pattern_bytes + 13] pattern lengths
static const uint8_t huffman_codes[] = {0x0, 0x3, 0x8, 0xb, 0x14, 0x24, 0x26, 0x2b, 0x4b, 0x54, 0x94, 0x95, 0xaa, 0x27};
// These bit lengths pre-add the 1 bit for the 0-value is_literal flag.
static const uint8_t huffman_bits[] = {0x2, 0x3, 0x5, 0x5, 0x6, 0x7, 0x7, 0x7, 0x8, 0x8, 0x9, 0x9, 0x9, 0x7};

#if TAMP_EXTENDED_COMPRESS
#define RLE_MAX_COUNT ((13 << 4) + 15 + 2)            // 225
#define EXTENDED_MATCH_MAX_EXTRA ((13 << 3) + 7 + 1)  // 112

// Minimum output buffer space required for extended match token.
// Extended match: symbol (7 bits) + extended huffman (11 bits) + window pos (15 bits) = 33 bits.
// With 7 bits in bit buffer, need up to 40 bits = 5 bytes. Add 1 byte margin.
// Pre-checking prevents OUTPUT_FULL mid-token, which would corrupt bit_buffer on retry.
#define EXTENDED_MATCH_MIN_OUTPUT_BYTES 6
#endif

static TAMP_NOINLINE void write_to_bit_buffer(TampCompressor *compressor, uint32_t bits, uint8_t n_bits) {
    compressor->bit_buffer_pos += n_bits;
    compressor->bit_buffer |= bits << (32 - compressor->bit_buffer_pos);
}

#if TAMP_EXTENDED_COMPRESS
/**
 * @brief Write extended huffman encoding (huffman + trailing bits).
 *
 * Used for both RLE count and extended match size encoding.
 *
 * @param[in,out] compressor Compressor with bit buffer.
 * @param[in] value The value to encode.
 * @param[in] trailing_bits Number of trailing bits (3 for extended match, 4 for RLE).
 */
static TAMP_NOINLINE void write_extended_huffman(TampCompressor *compressor, uint8_t value, uint8_t trailing_bits) {
    uint8_t code_index = value >> trailing_bits;
    // Write huffman code (without literal flag) + trailing bits in one call
    write_to_bit_buffer(compressor, (huffman_codes[code_index] << trailing_bits) | (value & ((1 << trailing_bits) - 1)),
                        (huffman_bits[code_index] - 1) + trailing_bits);
}

#endif  // TAMP_EXTENDED_COMPRESS

/**
 * @brief Partially flush the internal bit buffer.
 *
 * Flushes complete bytes from the bit buffer. Up to 7 bits may remain.
 */
static TAMP_NOINLINE tamp_res partial_flush(TampCompressor *compressor, unsigned char *output, size_t output_size,
                                            size_t *output_written_size) {
    for (*output_written_size = output_size; compressor->bit_buffer_pos >= 8 && output_size;
         output_size--, compressor->bit_buffer_pos -= 8, compressor->bit_buffer <<= 8)
        *output++ = compressor->bit_buffer >> 24;
    *output_written_size -= output_size;
    return (compressor->bit_buffer_pos >= 8) ? TAMP_OUTPUT_FULL : TAMP_OK;
}

inline bool tamp_compressor_full(const TampCompressor *compressor) {
    return compressor->input_size == sizeof(compressor->input);
}

/*
 * Platform-specific find_best_match implementations:
 *
 * 1. TAMP_ESP32: External implementation in espidf/tamp/compressor_esp32.cpp
 *
 * 2. Desktop 64-bit (x86_64, aarch64, Windows 64-bit):
 *    Included from compressor_find_match_desktop.c - uses bit manipulation
 *    and 64-bit loads for parallel match detection
 *
 * 3. Embedded/Default (Cortex-M0/M0+, other 32-bit):
 *    Defined below - single-byte-first comparison, safe for all architectures
 *
 * Set TAMP_USE_EMBEDDED_MATCH=1 to force the embedded implementation on desktop
 * (useful for testing the embedded code path on CI).
 */

#if TAMP_ESP32
extern void find_best_match(TampCompressor *compressor, uint16_t *match_index, uint8_t *match_size);

#elif (defined(__x86_64__) || defined(__aarch64__) || defined(_M_X64) || defined(_M_ARM64)) && !TAMP_USE_EMBEDDED_MATCH
#include "compressor_find_match_desktop.c"

#else
/**
 * @brief Find the best match for the current input buffer.
 *
 * Embedded/32-bit implementation: uses single-byte-first comparison (faster on simple cores).
 *
 * @param[in,out] compressor TampCompressor object to perform search on.
 * @param[out] match_index  If match_size is 0, this value is undefined.
 * @param[out] match_size Size of best found match.
 */
static inline void find_best_match(TampCompressor *compressor, uint16_t *match_index, uint8_t *match_size) {
    *match_size = 0;

    if (TAMP_UNLIKELY(compressor->input_size < compressor->min_pattern_size)) return;

    const uint8_t first_byte = read_input(0);
    const uint8_t second_byte = read_input(1);
    const uint32_t window_size_minus_1 = WINDOW_SIZE - 1;
    const uint8_t max_pattern_size = MIN(compressor->input_size, MAX_PATTERN_SIZE);
    const unsigned char *window = compressor->window;

    for (uint32_t window_index = 0; window_index < window_size_minus_1; window_index++) {
        if (TAMP_LIKELY(window[window_index] != first_byte)) {
            continue;
        }
        if (TAMP_LIKELY(window[window_index + 1] != second_byte)) {
            continue;
        }

        // Found 2-byte match, now extend the match
        uint8_t match_len = 2;

        // Extend match byte by byte with optimized bounds checking
        for (uint8_t i = 2; i < max_pattern_size; i++) {
            if (TAMP_UNLIKELY((window_index + i) > window_size_minus_1)) break;

            if (TAMP_LIKELY(window[window_index + i] != read_input(i))) break;
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
}

#endif

#if TAMP_LAZY_MATCHING
/**
 * @brief Check if writing a single byte will overlap with a future match section.
 *
 * @param[in] write_pos Position where the single byte will be written.
 * @param[in] match_index Index in window where the match starts.
 * @param[in] match_size Size of the match to validate.
 * @return true if no overlap (match is safe), false if there's overlap.
 */
static inline bool validate_no_match_overlap(uint16_t write_pos, uint16_t match_index, uint8_t match_size) {
    // Check if write position falls within the match range [match_index, match_index + match_size - 1]
    return write_pos < match_index || write_pos >= match_index + match_size;
}
#endif

tamp_res tamp_compressor_init(TampCompressor *compressor, const TampConf *conf, unsigned char *window) {
    const TampConf conf_default = {
        .window = 10,
        .literal = 8,
        .use_custom_dictionary = false,
#if TAMP_LAZY_MATCHING
        .lazy_matching = false,
#endif
#if TAMP_EXTENDED_COMPRESS
        .extended = true,  // Default to extended format
#endif
    };
    if (!conf) conf = &conf_default;
    if (conf->window < 8 || conf->window > 15) return TAMP_INVALID_CONF;
    if (conf->literal < 5 || conf->literal > 8) return TAMP_INVALID_CONF;
#if !TAMP_EXTENDED_COMPRESS
    if (conf->extended) return TAMP_INVALID_CONF;  // Extended requested but not compiled in
#endif

    for (uint8_t i = 0; i < sizeof(TampCompressor); i++)  // Zero-out the struct
        ((unsigned char *)compressor)[i] = 0;

    // Build header directly from conf (8 bits total)
    // Layout: [window:3][literal:2][use_custom_dictionary:1][extended:1][more_headers:1]
    uint8_t header = ((conf->window - 8) << 5) | ((conf->literal - 5) << 3) | (conf->use_custom_dictionary << 2) |
                     (conf->extended << 1);

    compressor->conf = *conf;  // Single struct copy
    compressor->window = window;
    compressor->min_pattern_size = tamp_compute_min_pattern_size(conf->window, conf->literal);

#if TAMP_LAZY_MATCHING
    compressor->cached_match_index = -1;  // Initialize cache as invalid
#endif

    if (!conf->use_custom_dictionary) tamp_initialize_dictionary(window, (1 << conf->window));

    write_to_bit_buffer(compressor, header, 8);

    return TAMP_OK;
}

#if TAMP_EXTENDED_COMPRESS
/**
 * @brief Get the last byte written to the window.
 */
static inline uint8_t get_last_window_byte(TampCompressor *compressor) {
    uint16_t prev_pos = (compressor->window_pos - 1) & ((1 << compressor->conf.window) - 1);
    return compressor->window[prev_pos];
}

/**
 * @brief Write RLE token to bit buffer and update window.
 *
 * @param[in,out] compressor Compressor state.
 * @param[in] count Number of repeated bytes (must be >= 2).
 */
static TAMP_NOINLINE void write_rle_token(TampCompressor *compressor, uint8_t count) {
    const uint16_t window_mask = (1 << compressor->conf.window) - 1;
    uint8_t symbol = get_last_window_byte(compressor);

    // Write RLE symbol (12) with literal flag
    // Note: symbols 12 and 13 are at indices 12 and 13 in huffman table (not offset by min_pattern_size)
    write_to_bit_buffer(compressor, huffman_codes[TAMP_RLE_SYMBOL], huffman_bits[TAMP_RLE_SYMBOL]);
    // Write extended huffman for count-2
    write_extended_huffman(compressor, count - 2, TAMP_LEADING_RLE_BITS);

    // Write up to TAMP_RLE_MAX_WINDOW bytes to window (or until buffer end, no wrap)
    uint16_t remaining = WINDOW_SIZE - compressor->window_pos;
    uint8_t window_write = MIN(MIN(count, TAMP_RLE_MAX_WINDOW), remaining);
    for (uint8_t i = 0; i < window_write; i++) {
        compressor->window[compressor->window_pos] = symbol;
        compressor->window_pos = (compressor->window_pos + 1) & window_mask;
    }
}

/**
 * @brief Write extended match token to bit buffer and update window.
 *
 * Token format: symbol (7 bits) + extended_huffman (up to 11 bits) + window_pos (up to 15 bits)
 * Total: up to 33 bits. We flush after symbol+huffman (18 bits max) to ensure window_pos fits.
 *
 * @param[in,out] compressor Compressor state.
 * @param[out] output Output buffer for flushed bytes.
 * @param[in] output_size Available space in output buffer.
 * @param[out] output_written_size Bytes written to output.
 * @return TAMP_OK on success, TAMP_OUTPUT_FULL if output buffer is too small.
 */
static TAMP_NOINLINE tamp_res write_extended_match_token(TampCompressor *compressor, unsigned char *output,
                                                         size_t output_size, size_t *output_written_size) {
    const uint16_t window_mask = (1 << compressor->conf.window) - 1;
    const uint8_t count = compressor->extended_match_count;
    const uint16_t position = compressor->extended_match_position;
    tamp_res res;
    size_t flush_bytes;

    *output_written_size = 0;

    // Write symbol (7 bits) + extended huffman (up to 11 bits) = 18 bits max
    // With ≤7 bits already in buffer, total ≤25 bits - fits in 32-bit buffer
    write_to_bit_buffer(compressor, huffman_codes[TAMP_EXTENDED_MATCH_SYMBOL],
                        huffman_bits[TAMP_EXTENDED_MATCH_SYMBOL]);
    write_extended_huffman(compressor, count - compressor->min_pattern_size - 11 - 1, TAMP_LEADING_EXTENDED_MATCH_BITS);

    // Flush to make room for window position (up to 15 bits)
    res = partial_flush(compressor, output, output_size, &flush_bytes);
    *output_written_size += flush_bytes;
    output += flush_bytes;
    output_size -= flush_bytes;
    if (TAMP_UNLIKELY(res != TAMP_OK)) return res;

    // Write window position - with ≤7 bits remaining, up to 22 bits total - fits
    write_to_bit_buffer(compressor, position, compressor->conf.window);

    // Final flush
    res = partial_flush(compressor, output, output_size, &flush_bytes);
    *output_written_size += flush_bytes;
    if (TAMP_UNLIKELY(res != TAMP_OK)) return res;

    // Write to window (up to end of buffer, no wrap)
    uint16_t remaining = WINDOW_SIZE - compressor->window_pos;
    uint8_t window_write = MIN(count, remaining);
    tamp_window_copy(compressor->window, &compressor->window_pos, position, window_write, window_mask);

    // Reset extended match state
    compressor->extended_match_count = 0;
    compressor->extended_match_position = 0;

    return TAMP_OK;
}
#endif  // TAMP_EXTENDED_COMPRESS

TAMP_NOINLINE tamp_res tamp_compressor_poll(TampCompressor *compressor, unsigned char *output, size_t output_size,
                                            size_t *output_written_size) {
    tamp_res res;
    // Cache bitfield values for faster access in hot path
    const uint8_t conf_window = compressor->conf.window;
    const uint8_t conf_literal = compressor->conf.literal;
    const uint16_t window_mask = (1 << conf_window) - 1;
#if TAMP_EXTENDED_COMPRESS
    const bool conf_extended = compressor->conf.extended;
#endif
    size_t output_written_size_proxy;

    if (!output_written_size) output_written_size = &output_written_size_proxy;
    *output_written_size = 0;

    if (TAMP_UNLIKELY(compressor->input_size == 0)) return TAMP_OK;

    {
        // Make sure there's enough room in the bit buffer.
        size_t flush_bytes_written;
        res = partial_flush(compressor, output, output_size, &flush_bytes_written);
        (*output_written_size) += flush_bytes_written;
        if (TAMP_UNLIKELY(res != TAMP_OK)) return res;
        output_size -= flush_bytes_written;
        output += flush_bytes_written;  // cppcheck-suppress unreadVariable
    }

    if (TAMP_UNLIKELY(output_size == 0)) return TAMP_OUTPUT_FULL;

    uint8_t match_size = 0;
    uint16_t match_index = 0;

#if TAMP_EXTENDED_COMPRESS
    // Extended: Handle extended match continuation
    if (TAMP_UNLIKELY(conf_extended && compressor->extended_match_count)) {
        // We're in extended match mode - try to extend the match at the current position
        const uint8_t max_ext_match = compressor->min_pattern_size + 11 + EXTENDED_MATCH_MAX_EXTRA;
        const unsigned char *window = compressor->window;

        while (compressor->input_size > 0) {
            const uint16_t current_pos = compressor->extended_match_position;
            const uint8_t current_count = compressor->extended_match_count;

            // Check if extending would go beyond window buffer boundary (no wrap-around)
            if (current_pos + current_count >= WINDOW_SIZE) {
                // Pre-check output space to prevent OUTPUT_FULL mid-token (would corrupt bit_buffer)
                if (TAMP_UNLIKELY(output_size < EXTENDED_MATCH_MIN_OUTPUT_BYTES)) return TAMP_OUTPUT_FULL;
                size_t token_bytes;
                res = write_extended_match_token(compressor, output, output_size, &token_bytes);
                (*output_written_size) += token_bytes;
                if (TAMP_UNLIKELY(res != TAMP_OK)) return res;
                return TAMP_OK;
            }

            // Check if we've reached max extended match size
            if (current_count >= max_ext_match) {
                // Pre-check output space to prevent OUTPUT_FULL mid-token (would corrupt bit_buffer)
                if (TAMP_UNLIKELY(output_size < EXTENDED_MATCH_MIN_OUTPUT_BYTES)) return TAMP_OUTPUT_FULL;
                size_t token_bytes;
                res = write_extended_match_token(compressor, output, output_size, &token_bytes);
                (*output_written_size) += token_bytes;
                if (TAMP_UNLIKELY(res != TAMP_OK)) return res;
                return TAMP_OK;
            }

            // O(1) extension check: does the next byte at current position match input?
            if (window[current_pos + current_count] == read_input(0)) {
                // Extension successful - consume input byte and increment count
                compressor->extended_match_count++;
                compressor->input_pos = input_add(1);
                compressor->input_size--;
                // Continue to next iteration to try extending further
            } else {
                // Match ended - emit current match
                // Pre-check output space to prevent OUTPUT_FULL mid-token (would corrupt bit_buffer)
                if (TAMP_UNLIKELY(output_size < EXTENDED_MATCH_MIN_OUTPUT_BYTES)) return TAMP_OUTPUT_FULL;
                size_t token_bytes;
                res = write_extended_match_token(compressor, output, output_size, &token_bytes);
                (*output_written_size) += token_bytes;
                if (TAMP_UNLIKELY(res != TAMP_OK)) return res;
                return TAMP_OK;
            }
        }
        // Ran out of input while extending - return and wait for more
        return TAMP_OK;
    }

    // Extended: Handle RLE accumulation with persistent state
    if (TAMP_UNLIKELY(conf_extended)) {
        uint8_t last_byte = get_last_window_byte(compressor);

        // Count and CONSUME matching bytes
        while (compressor->input_size > 0 && compressor->rle_count < RLE_MAX_COUNT) {
            if (read_input(0) == last_byte) {
                compressor->rle_count++;
                compressor->input_pos = input_add(1);
                compressor->input_size--;
            } else {
                break;
            }
        }

        // If we consumed whole buffer and haven't hit max, return (accumulate more)
        if (compressor->input_size == 0 && compressor->rle_count < RLE_MAX_COUNT && compressor->rle_count > 0) {
            return TAMP_OK;
        }

        // RLE run has ended
        if (compressor->rle_count >= 2) {
            // Commit the RLE (simplified approach for C)
            write_rle_token(compressor, compressor->rle_count);
            compressor->rle_count = 0;
            return TAMP_OK;
        } else if (compressor->rle_count == 1) {
            // Single byte - push it back to input for normal literal encoding
            compressor->input_pos = input_add(-1);
            compressor->input_size++;
            compressor->rle_count = 0;
        }
    }
#endif  // TAMP_EXTENDED_COMPRESS

#if TAMP_LAZY_MATCHING
    if (compressor->conf.lazy_matching) {
        // Check if we have a cached match from lazy matching
        if (TAMP_UNLIKELY(compressor->cached_match_index >= 0)) {
            match_index = compressor->cached_match_index;
            match_size = compressor->cached_match_size;
            compressor->cached_match_index = -1;  // Clear cache after using
        } else {
            find_best_match(compressor, &match_index, &match_size);
        }

        // Lazy matching: if we have a good match, check if position i+1 has a better match
        if (match_size >= compressor->min_pattern_size && match_size <= 8 && compressor->input_size > match_size + 2) {
            // Temporarily advance input position to check next position
            compressor->input_pos = input_add(1);
            compressor->input_size--;

            uint8_t next_match_size = 0;
            uint16_t next_match_index = 0;
            find_best_match(compressor, &next_match_index, &next_match_size);

            // Restore input position
            compressor->input_pos = input_add(-1);
            compressor->input_size++;

            // If next position has a better match, and the match doesn't overlap with the literal we are writing, emit
            // literal and cache the next match
            if (next_match_size > match_size &&
                validate_no_match_overlap(compressor->window_pos, next_match_index, next_match_size)) {
                // Force literal at current position, cache next match
                compressor->cached_match_index = next_match_index;
                compressor->cached_match_size = next_match_size;
                match_size = 0;  // Will trigger literal write below
            } else {
                compressor->cached_match_index = -1;
                // Note: No V2 extended match check here - we're in the match_size <= 8 branch,
                // so extended matches (which require match_size > min_pattern_size + 11) are impossible.
            }
        } else {
            compressor->cached_match_index = -1;  // Clear cache
        }
    } else {
        find_best_match(compressor, &match_index, &match_size);
    }
#else
    find_best_match(compressor, &match_index, &match_size);
#endif

    // Shared token/literal writing logic
    if (TAMP_UNLIKELY(match_size < compressor->min_pattern_size)) {
        // Write LITERAL
        match_size = 1;
        unsigned char c = read_input(0);
        if (TAMP_UNLIKELY(c >> conf_literal)) {
            return TAMP_EXCESS_BITS;
        }
        write_to_bit_buffer(compressor, (1 << conf_literal) | c, conf_literal + 1);
    } else {
#if TAMP_EXTENDED_COMPRESS
        // Extended: Start extended match continuation
        if (conf_extended && match_size > compressor->min_pattern_size + 11) {
            compressor->extended_match_count = match_size;
            compressor->extended_match_position = match_index;
            // Consume matched bytes from input
            compressor->input_pos = input_add(match_size);
            compressor->input_size -= match_size;
            // Return - continuation code at start of poll will try to extend or emit
            return TAMP_OK;
        }
#endif  // TAMP_EXTENDED_COMPRESS
        // Write TOKEN (huffman code + window position)
        uint8_t huffman_index = match_size - compressor->min_pattern_size;
        write_to_bit_buffer(compressor, (huffman_codes[huffman_index] << conf_window) | match_index,
                            huffman_bits[huffman_index] + conf_window);
    }
    // Populate Window
    for (uint8_t i = 0; i < match_size; i++) {
        compressor->window[compressor->window_pos] = read_input(0);
        compressor->window_pos = (compressor->window_pos + 1) & window_mask;
        compressor->input_pos = input_add(1);
    }
    compressor->input_size -= match_size;

    return TAMP_OK;
}

void tamp_compressor_sink(TampCompressor *compressor, const unsigned char *input, size_t input_size,
                          size_t *consumed_size) {
    size_t consumed_size_proxy;
    if (TAMP_LIKELY(consumed_size))
        *consumed_size = 0;
    else
        consumed_size = &consumed_size_proxy;

    for (size_t i = 0; i < input_size; i++) {
        if (TAMP_UNLIKELY(tamp_compressor_full(compressor))) break;
        compressor->input[input_add(compressor->input_size)] = input[i];
        compressor->input_size += 1;
        (*consumed_size)++;
    }
}

tamp_res tamp_compressor_compress_cb(TampCompressor *compressor, unsigned char *output, size_t output_size,
                                     size_t *output_written_size, const unsigned char *input, size_t input_size,
                                     size_t *input_consumed_size, tamp_callback_t callback, void *user_data) {
    tamp_res res;
    size_t input_consumed_size_proxy = 0, output_written_size_proxy = 0;
    size_t total_input_size = input_size;

    if (TAMP_LIKELY(output_written_size))
        *output_written_size = 0;
    else
        output_written_size = &output_written_size_proxy;

    if (TAMP_LIKELY(input_consumed_size))
        *input_consumed_size = 0;
    else
        input_consumed_size = &input_consumed_size_proxy;

    while (input_size > 0 && output_size > 0) {
        {
            // Sink Data into input buffer.
            size_t consumed;
            tamp_compressor_sink(compressor, input, input_size, &consumed);
            input += consumed;
            input_size -= consumed;
            (*input_consumed_size) += consumed;
        }
        if (TAMP_LIKELY(tamp_compressor_full(compressor))) {
            // Input buffer is full and ready to start compressing.
            size_t chunk_output_written_size;
            res = tamp_compressor_poll(compressor, output, output_size, &chunk_output_written_size);
            output += chunk_output_written_size;
            output_size -= chunk_output_written_size;
            (*output_written_size) += chunk_output_written_size;
            if (TAMP_UNLIKELY(res != TAMP_OK)) return res;
            if (TAMP_UNLIKELY(callback && (res = callback(user_data, *output_written_size, total_input_size))))
                return (tamp_res)res;
        }
    }
    return TAMP_OK;
}

tamp_res tamp_compressor_flush(TampCompressor *compressor, unsigned char *output, size_t output_size,
                               size_t *output_written_size, bool write_token) {
    tamp_res res;
    size_t chunk_output_written_size;
    size_t output_written_size_proxy;

    if (!output_written_size) output_written_size = &output_written_size_proxy;
    *output_written_size = 0;

    while (compressor->input_size) {
        // Compress the remainder of the input buffer.
        res = tamp_compressor_poll(compressor, output, output_size, &chunk_output_written_size);
        (*output_written_size) += chunk_output_written_size;
        if (TAMP_UNLIKELY(res != TAMP_OK)) return res;
        output_size -= chunk_output_written_size;
        output += chunk_output_written_size;
    }

#if TAMP_EXTENDED_COMPRESS
    // Extended: Flush any pending RLE
    if (compressor->conf.extended && compressor->rle_count >= 1) {
        // Partial flush first to make room
        res = partial_flush(compressor, output, output_size, &chunk_output_written_size);
        (*output_written_size) += chunk_output_written_size;
        if (TAMP_UNLIKELY(res != TAMP_OK)) return res;
        output_size -= chunk_output_written_size;
        output += chunk_output_written_size;

        if (compressor->rle_count == 1) {
            // Single byte - write as literal (can't use RLE token for count < 2)
            uint8_t literal = get_last_window_byte(compressor);
            write_to_bit_buffer(compressor, IS_LITERAL_FLAG | literal, compressor->conf.literal + 1);

            // Write to window
            const uint16_t window_mask = (1 << compressor->conf.window) - 1;
            compressor->window[compressor->window_pos] = literal;
            compressor->window_pos = (compressor->window_pos + 1) & window_mask;
        } else {
            // count >= 2: write as RLE token
            write_rle_token(compressor, compressor->rle_count);
        }
        compressor->rle_count = 0;

        // Partial flush again after writing token
        res = partial_flush(compressor, output, output_size, &chunk_output_written_size);
        (*output_written_size) += chunk_output_written_size;
        if (TAMP_UNLIKELY(res != TAMP_OK)) return res;
        output_size -= chunk_output_written_size;
        output += chunk_output_written_size;
    }

    // Extended: Flush any pending extended match
    if (compressor->conf.extended && compressor->extended_match_count) {
        // Pre-check output space to prevent OUTPUT_FULL mid-token (would corrupt bit_buffer)
        if (TAMP_UNLIKELY(output_size < EXTENDED_MATCH_MIN_OUTPUT_BYTES)) return TAMP_OUTPUT_FULL;
        res = write_extended_match_token(compressor, output, output_size, &chunk_output_written_size);
        (*output_written_size) += chunk_output_written_size;
        if (TAMP_UNLIKELY(res != TAMP_OK)) return res;
        output_size -= chunk_output_written_size;
        output += chunk_output_written_size;
    }
#endif  // TAMP_EXTENDED_COMPRESS

    // Perform partial flush to see if we need a FLUSH token (check if output buffer in not empty),
    // and to subsequently make room for the FLUSH token.
    res = partial_flush(compressor, output, output_size, &chunk_output_written_size);
    output_size -= chunk_output_written_size;
    (*output_written_size) += chunk_output_written_size;
    output += chunk_output_written_size;
    if (TAMP_UNLIKELY(res != TAMP_OK)) return res;

    // Check if there's enough output buffer space
    if (compressor->bit_buffer_pos) {
        if (output_size == 0) {
            return TAMP_OUTPUT_FULL;
        }
        if (write_token) {
            if (output_size < 2) return TAMP_OUTPUT_FULL;
            write_to_bit_buffer(compressor, FLUSH_CODE, 9);
        }
    }

    // Flush the remainder of the output bit-buffer
    while (compressor->bit_buffer_pos) {
        *output = compressor->bit_buffer >> 24;
        output++;
        compressor->bit_buffer <<= 8;
        compressor->bit_buffer_pos -= MIN(compressor->bit_buffer_pos, 8);
        output_size--;
        (*output_written_size)++;
    }

    return TAMP_OK;
}

tamp_res tamp_compressor_compress_and_flush_cb(TampCompressor *compressor, unsigned char *output, size_t output_size,
                                               size_t *output_written_size, const unsigned char *input,
                                               size_t input_size, size_t *input_consumed_size, bool write_token,
                                               tamp_callback_t callback, void *user_data) {
    tamp_res res;
    size_t flush_size;
    size_t output_written_size_proxy;

    if (!output_written_size) output_written_size = &output_written_size_proxy;

    res = tamp_compressor_compress_cb(compressor, output, output_size, output_written_size, input, input_size,
                                      input_consumed_size, callback, user_data);
    if (TAMP_UNLIKELY(res != TAMP_OK)) return res;

    res = tamp_compressor_flush(compressor, output + *output_written_size, output_size - *output_written_size,
                                &flush_size, write_token);

    (*output_written_size) += flush_size;

    if (TAMP_UNLIKELY(res != TAMP_OK)) return res;

    return TAMP_OK;
}

#if TAMP_STREAM

tamp_res tamp_compress_stream(TampCompressor *compressor, tamp_read_t read_cb, void *read_handle, tamp_write_t write_cb,
                              void *write_handle, size_t *input_consumed_size, size_t *output_written_size,
                              tamp_callback_t callback, void *user_data) {
    size_t input_consumed_size_proxy, output_written_size_proxy;
    if (!input_consumed_size) input_consumed_size = &input_consumed_size_proxy;
    if (!output_written_size) output_written_size = &output_written_size_proxy;
    *input_consumed_size = 0;
    *output_written_size = 0;

    unsigned char input_buffer[TAMP_STREAM_WORK_BUFFER_SIZE / 2];
    unsigned char output_buffer[TAMP_STREAM_WORK_BUFFER_SIZE / 2];

    // Main compression loop
    while (1) {
        int bytes_read = read_cb(read_handle, input_buffer, sizeof(input_buffer));
        if (TAMP_UNLIKELY(bytes_read < 0)) return TAMP_READ_ERROR;
        if (bytes_read == 0) break;

        *input_consumed_size += bytes_read;

        size_t input_pos = 0;
        while (input_pos < (size_t)bytes_read) {
            size_t chunk_consumed, chunk_written;

            tamp_res res = tamp_compressor_compress(compressor, output_buffer, sizeof(output_buffer), &chunk_written,
                                                    input_buffer + input_pos, bytes_read - input_pos, &chunk_consumed);
            if (TAMP_UNLIKELY(res < TAMP_OK)) return res;

            input_pos += chunk_consumed;

            if (TAMP_LIKELY(chunk_written > 0)) {
                int bytes_written = write_cb(write_handle, output_buffer, chunk_written);
                if (TAMP_UNLIKELY(bytes_written < 0 || (size_t)bytes_written != chunk_written)) {
                    return TAMP_WRITE_ERROR;
                }
                *output_written_size += chunk_written;
            }
        }

        if (TAMP_UNLIKELY(callback)) {
            int cb_res = callback(user_data, *input_consumed_size, 0);
            if (TAMP_UNLIKELY(cb_res)) return (tamp_res)cb_res;
        }
    }

    // Flush remaining data
    while (1) {
        size_t chunk_written;
        tamp_res res = tamp_compressor_flush(compressor, output_buffer, sizeof(output_buffer), &chunk_written, false);
        if (TAMP_UNLIKELY(res < TAMP_OK)) return res;

        if (TAMP_LIKELY(chunk_written > 0)) {
            int bytes_written = write_cb(write_handle, output_buffer, chunk_written);
            if (TAMP_UNLIKELY(bytes_written < 0 || (size_t)bytes_written != chunk_written)) {
                return TAMP_WRITE_ERROR;
            }
            *output_written_size += chunk_written;
        }

        if (res == TAMP_OK) break;
    }

    return TAMP_OK;
}

#endif /* TAMP_STREAM */
