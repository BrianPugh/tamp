#include "compressor.h"

#include <stdbool.h>
#include <stdlib.h>

#include "common.h"

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2 * !!(condition)]))

#define MAX_PATTERN_SIZE (compressor->min_pattern_size + 13)
#define WINDOW_SIZE (1 << compressor->conf_window)
// 0xF because sizeof(TampCompressor.input) == 16;
#define input_add(offset) ((compressor->input_pos + offset) & 0xF)
#define read_input(offset) (compressor->input[input_add(offset)])
#define IS_LITERAL_FLAG (1 << compressor->conf_literal)

#define FLUSH_CODE (0xAB)

// Huffman codes for encoding match sizes
// Encodes [min_pattern_bytes, min_pattern_bytes + 13] pattern lengths
// In v2: indices 12 and 13 are repurposed for RLE and extended match, index 14 is added
static const uint8_t huffman_codes[] = {0x0,  0x3,  0x8,  0xb,  0x14, 0x24, 0x26, 0x2b,
                                        0x4b, 0x54, 0x94, 0x95, 0xaa, 0x27, 0xab};
// Bit lengths include the 1 bit for the 0-value is_literal flag
static const uint8_t huffman_bits[] = {0x2, 0x3, 0x5, 0x5, 0x6, 0x7, 0x7, 0x7, 0x8, 0x8, 0x9, 0x9, 0x9, 0x7, 0x9};

static inline void write_to_bit_buffer(TampCompressor *compressor, uint32_t bits, uint8_t n_bits) {
    compressor->bit_buffer_pos += n_bits;
    compressor->bit_buffer |= bits << (32 - compressor->bit_buffer_pos);
}

/**
 * @brief Write extended Huffman encoded value to bit buffer.
 *
 * Used for v2 RLE and extended match encoding.
 *
 * @param[in,out] bit_buffer Pointer to the bit buffer.
 * @param[in,out] bit_buffer_pos Pointer to current bit position in buffer.
 * @param[in] value Value to encode.
 * @param[in] leading_bits Number of leading bits (3 for extended match, 4 for RLE).
 */
static void tamp_write_extended_huffman(uint32_t *bit_buffer, uint8_t *bit_buffer_pos, uint32_t value,
                                        uint8_t leading_bits) {
    uint8_t mask = (1 << leading_bits) - 1;
    uint8_t code_index = value >> leading_bits;
    uint8_t lower_bits = value & mask;

    *bit_buffer_pos += huffman_bits[code_index] - 1;
    *bit_buffer |= huffman_codes[code_index] << (32 - *bit_buffer_pos);

    *bit_buffer_pos += leading_bits;
    *bit_buffer |= lower_bits << (32 - *bit_buffer_pos);
}

/**
 * @brief Partially flush the internal bit buffer.
 *
 * Up to 7 bits may remain in the internal bit buffer.
 */
static inline tamp_res partial_flush(TampCompressor *compressor, unsigned char *output, size_t output_size,
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

#if TAMP_ESP32
extern void find_best_match(TampCompressor *compressor, uint16_t *match_index, uint8_t *match_size);
#else
/**
 * @brief Find the best match for the current input buffer.
 *
 * @param[in,out] compressor TampCompressor object to perform search on.
 * @param[out] match_index  If match_size is 0, this value is undefined.
 * @param[out] match_size Size of best found match.
 */
static inline void find_best_match(TampCompressor *compressor, uint16_t *match_index, uint8_t *match_size) {
    *match_size = 0;

    if (TAMP_UNLIKELY(compressor->input_size < compressor->min_pattern_size)) return;

    const uint16_t first_second = (read_input(0) << 8) | read_input(1);
    const uint16_t window_size_minus_1 = WINDOW_SIZE - 1;
    const uint8_t max_pattern_size = MIN(compressor->input_size, MAX_PATTERN_SIZE);

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

/**
 * @brief Compute RLE breakeven point for v2 compression.
 *
 * Determines the pattern size at which pattern matching becomes more efficient than RLE.
 *
 * @param[in] min_pattern_size Minimum pattern size.
 * @param[in] window_bits Number of window bits.
 * @return Breakeven pattern size.
 */
static uint8_t compute_rle_breakeven(uint8_t min_pattern_size, uint8_t window_bits) {
    uint8_t breakeven = 0;

    for (uint8_t i = min_pattern_size; i <= min_pattern_size + 11; i++) {
        uint8_t rle_bits = huffman_bits[(i - 1) >> LEADING_RLE_HUFFMAN_BITS] + LEADING_RLE_HUFFMAN_BITS;
        uint8_t pattern_bits = huffman_bits[i - min_pattern_size] + window_bits;

        if (pattern_bits < rle_bits) {
            breakeven = i;
        }
    }

    return breakeven;
}

tamp_res tamp_compressor_init(TampCompressor *compressor, const TampConf *conf, unsigned char *window) {
    const TampConf conf_default = {
        .window = 10,
        .literal = 8,
        .use_custom_dictionary = false,
        .v2 = true,  // v2 compression with RLE is now implemented (extended matches TODO)
#if TAMP_LAZY_MATCHING
        .lazy_matching = false,
#endif
    };
    if (!conf) conf = &conf_default;
    if (conf->window < 8 || conf->window > 15) return TAMP_INVALID_CONF;
    if (conf->literal < 5 || conf->literal > 8) return TAMP_INVALID_CONF;

    for (uint8_t i = 0; i < sizeof(TampCompressor); i++)  // Zero-out the struct
        ((unsigned char *)compressor)[i] = 0;

    compressor->conf_literal = conf->literal;
    compressor->conf_window = conf->window;
    compressor->conf_use_custom_dictionary = conf->use_custom_dictionary;
    compressor->conf_v2 = conf->v2;
#if TAMP_LAZY_MATCHING
    compressor->conf_lazy_matching = conf->lazy_matching;
#endif

    compressor->window = window;
    compressor->min_pattern_size = tamp_compute_min_pattern_size(conf->window, conf->literal);

    // Initialize v2 fields
    if (compressor->conf_v2) {
        compressor->count = 0;
        compressor->rle_breakeven = compute_rle_breakeven(compressor->min_pattern_size, conf->window);
        compressor->extended_match_position = 0;
        compressor->write_state = TAMP_WRITE_STATE_NORMAL;
    }

#if TAMP_LAZY_MATCHING
    compressor->cached_match_index = -1;  // Initialize cache as invalid
#endif

    if (!compressor->conf_use_custom_dictionary) tamp_initialize_dictionary(window, (1 << conf->window));

    // Write header to bit buffer
    write_to_bit_buffer(compressor, conf->window - 8, 3);
    write_to_bit_buffer(compressor, conf->literal - 5, 2);
    write_to_bit_buffer(compressor, conf->use_custom_dictionary, 1);
    write_to_bit_buffer(compressor, conf->v2, 1);
    write_to_bit_buffer(compressor, 0, 1);  // No more header bytes

    return TAMP_OK;
}

/**
 * @brief Write RLE token to bit buffer.
 *
 * @param[in,out] compressor TampCompressor object.
 */
__attribute__((unused)) static inline void write_rle(TampCompressor *compressor) {
    const uint16_t window_mask = (1 << compressor->conf_window) - 1;
    if (compressor->count == 0) {
        return;  // Nothing to write
    } else if (compressor->count == 1) {
        // Just write a literal
        unsigned char rle_byte = compressor->window[(compressor->window_pos - 1) & window_mask];
        write_to_bit_buffer(compressor, IS_LITERAL_FLAG | rle_byte, compressor->conf_literal + 1);
        compressor->window[compressor->window_pos] = rle_byte;
        compressor->window_pos = (compressor->window_pos + 1) & window_mask;
        compressor->write_state = TAMP_WRITE_STATE_NORMAL;
    } else {
        // Write RLE token (symbol 12)
        write_to_bit_buffer(compressor, huffman_codes[RLE_SYMBOL], huffman_bits[RLE_SYMBOL]);

        // Use temporary variable for bitfield compatibility
        uint8_t temp_bit_pos = compressor->bit_buffer_pos;
        tamp_write_extended_huffman(&compressor->bit_buffer, &temp_bit_pos, compressor->count - 2,
                                    LEADING_RLE_HUFFMAN_BITS);
        compressor->bit_buffer_pos = temp_bit_pos;

        if (compressor->write_state != TAMP_WRITE_STATE_RLE_WRITTEN) {
            // Only write up to 8 bytes to window, and only if we didn't already do this
            unsigned char rle_byte = compressor->window[(compressor->window_pos - 1) & window_mask];
            uint16_t bytes_to_write = MIN(compressor->count, RLE_MAX_WINDOW);
            for (uint16_t i = 0; i < bytes_to_write; i++) {
                compressor->window[compressor->window_pos] = rle_byte;
                compressor->window_pos = (compressor->window_pos + 1) & window_mask;
            }
        }

        compressor->write_state = TAMP_WRITE_STATE_RLE_WRITTEN;
    }

    compressor->count = 0;
}

/**
 * @brief Write extended match token to bit buffer.
 *
 * @param[in,out] compressor TampCompressor object.
 */
__attribute__((unused)) static inline void write_extended_match(TampCompressor *compressor) {
    // Write extended match token (symbol 13)
    write_to_bit_buffer(compressor, huffman_codes[EXTENDED_MATCH_SYMBOL], huffman_bits[EXTENDED_MATCH_SYMBOL]);

    // Write match position
    write_to_bit_buffer(compressor, compressor->extended_match_position, compressor->conf_window);

    // Write extended huffman encoded match size
    uint32_t encoded_size = compressor->count - compressor->min_pattern_size - 11 - 1;

    // Use temporary variable for bitfield compatibility
    uint8_t temp_bit_pos = compressor->bit_buffer_pos;
    tamp_write_extended_huffman(&compressor->bit_buffer, &temp_bit_pos, encoded_size,
                                LEADING_EXTENDED_MATCH_HUFFMAN_BITS);
    compressor->bit_buffer_pos = temp_bit_pos;

    // Copy from window to window
    const uint16_t window_mask = (1 << compressor->conf_window) - 1;
    for (uint16_t i = 0; i < compressor->count; i++) {
        uint16_t src_pos = (compressor->extended_match_position + i) & window_mask;
        compressor->window[compressor->window_pos] = compressor->window[src_pos];
        compressor->window_pos = (compressor->window_pos + 1) & window_mask;
    }

    // Reset state
    compressor->count = 0;
    compressor->extended_match_position = 0;
    compressor->write_state = TAMP_WRITE_STATE_NORMAL;
}

tamp_res tamp_compressor_poll(TampCompressor *compressor, unsigned char *output, size_t output_size,
                              size_t *output_written_size) {
    tamp_res res;
    const uint16_t window_mask = (1 << compressor->conf_window) - 1;
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

#if TAMP_EXTENDED_MATCH
    // V2: Try to extend ongoing extended match
    if (compressor->conf_v2 && compressor->write_state == TAMP_WRITE_STATE_EXTENDING_MATCH) {
        if (TAMP_UNLIKELY(compressor->input_size == 0)) return TAMP_OK;  // Need more input to continue

        unsigned char current_byte = read_input(0);
        uint16_t next_pos = (compressor->extended_match_position + compressor->count) & window_mask;
        uint16_t max_extended_match_size = compressor->min_pattern_size + EXTENDED_MATCH_ADDITIONAL;

        // Check if current byte matches next byte in window
        if (compressor->window[next_pos] == current_byte) {
            // Match continues - extend it
            compressor->count++;

            // Consume input byte
            compressor->input_pos = input_add(1);
            compressor->input_size--;

            // Check if we've hit the maximum
            if (compressor->count >= max_extended_match_size) {
                // Start writing the match
                write_to_bit_buffer(compressor, huffman_codes[EXTENDED_MATCH_SYMBOL],
                                    huffman_bits[EXTENDED_MATCH_SYMBOL]);
                write_to_bit_buffer(compressor, compressor->extended_match_position, compressor->conf_window);
                compressor->write_state = TAMP_WRITE_STATE_EXTENDED_MATCH_PENDING;
            }

            return TAMP_OK;
        } else {
            // Match ended - start writing
            write_to_bit_buffer(compressor, huffman_codes[EXTENDED_MATCH_SYMBOL], huffman_bits[EXTENDED_MATCH_SYMBOL]);
            write_to_bit_buffer(compressor, compressor->extended_match_position, compressor->conf_window);
            compressor->write_state = TAMP_WRITE_STATE_EXTENDED_MATCH_PENDING;
            return TAMP_OK;
        }
    }

    // V2: Resume multi-phase extended match write
    if (compressor->conf_v2 && compressor->write_state == TAMP_WRITE_STATE_EXTENDED_MATCH_PENDING) {
        // Write extended huffman encoded match size
        uint32_t encoded_size = compressor->count - compressor->min_pattern_size - 11 - 1;

        uint8_t temp_bit_pos = compressor->bit_buffer_pos;
        tamp_write_extended_huffman(&compressor->bit_buffer, &temp_bit_pos, encoded_size,
                                    LEADING_EXTENDED_MATCH_HUFFMAN_BITS);
        compressor->bit_buffer_pos = temp_bit_pos;

        // Copy from window to window
        for (uint16_t i = 0; i < compressor->count; i++) {
            uint16_t src_pos = (compressor->extended_match_position + i) & window_mask;
            compressor->window[compressor->window_pos] = compressor->window[src_pos];
            compressor->window_pos = (compressor->window_pos + 1) & window_mask;
        }

        // Reset state
        compressor->count = 0;
        compressor->extended_match_position = 0;
        compressor->write_state = TAMP_WRITE_STATE_NORMAL;

        return TAMP_OK;
    }
#endif

    // V2: RLE detection logic
    if (compressor->conf_v2) {
        unsigned char current_byte = read_input(0);

        // Check if current byte continues RLE sequence
        unsigned char last_written = compressor->window[(compressor->window_pos - 1) & window_mask];
        if (current_byte == last_written && compressor->count < RLE_MAX_SIZE) {
            compressor->count++;

            // Consume the byte
            compressor->input_pos = input_add(1);
            compressor->input_size--;

            return TAMP_OK;  // Continue accumulating RLE
        }

        // RLE sequence broken - check if we should emit accumulated RLE
        if (compressor->count >= 1) {
            // We have pending RLE - write it (write_rle handles count==1 as a literal)
            write_rle(compressor);
            return TAMP_OK;
        }
    }

    uint8_t match_size = 0;
    uint16_t match_index = 0;

#if TAMP_LAZY_MATCHING
    if (compressor->conf_lazy_matching) {
        // Check if we have a cached match from lazy matching
        if (TAMP_UNLIKELY(compressor->cached_match_index >= 0)) {
            match_index = compressor->cached_match_index;
            match_size = compressor->cached_match_size;
            compressor->cached_match_index = -1;  // Clear cache after using
        } else {
            find_best_match(compressor, &match_index, &match_size);
        }
    } else {
        find_best_match(compressor, &match_index, &match_size);
    }
#else
    find_best_match(compressor, &match_index, &match_size);
#endif

#if TAMP_LAZY_MATCHING
    if (compressor->conf_lazy_matching) {
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
                // Write LITERAL at current position
                match_size = 1;
                unsigned char c = read_input(0);
                if (TAMP_UNLIKELY(c >> compressor->conf_literal)) {
                    return TAMP_EXCESS_BITS;
                }
                write_to_bit_buffer(compressor, IS_LITERAL_FLAG | c, compressor->conf_literal + 1);

                if (compressor->conf_v2) {
                    compressor->write_state = TAMP_WRITE_STATE_NORMAL;
                }
            } else {
                // Use current match, clear cache
                compressor->cached_match_index = -1;
                uint8_t huffman_index = match_size - compressor->min_pattern_size;
                write_to_bit_buffer(compressor, huffman_codes[huffman_index], huffman_bits[huffman_index]);
                write_to_bit_buffer(compressor, match_index, compressor->conf_window);
            }
        } else if (TAMP_UNLIKELY(match_size < compressor->min_pattern_size)) {
            // Write LITERAL
            compressor->cached_match_index = -1;  // Clear cache
            match_size = 1;
            unsigned char c = read_input(0);
            if (TAMP_UNLIKELY(c >> compressor->conf_literal)) {
                return TAMP_EXCESS_BITS;
            }
            write_to_bit_buffer(compressor, IS_LITERAL_FLAG | c, compressor->conf_literal + 1);

            if (compressor->conf_v2) {
                compressor->write_state = TAMP_WRITE_STATE_NORMAL;
            }
        } else {
            // Write TOKEN
            compressor->cached_match_index = -1;  // Clear cache

#if TAMP_EXTENDED_MATCH
            // V2: Use extended match for large matches
            if (compressor->conf_v2 && match_size > (compressor->min_pattern_size + 11)) {
                // Store extended match info and enter extension mode
                compressor->extended_match_position = match_index;
                compressor->count = match_size;

                // Consume the initial match from input buffer
                for (uint8_t i = 0; i < match_size; i++) {
                    compressor->input_pos = input_add(1);
                }
                compressor->input_size -= match_size;

                // Enter extension mode - will try to extend on next poll
                compressor->write_state = TAMP_WRITE_STATE_EXTENDING_MATCH;

                return TAMP_OK;
            } else
#endif
            {
                // Regular v1 match or small v2 match
                uint8_t huffman_index = match_size - compressor->min_pattern_size;
                write_to_bit_buffer(compressor, huffman_codes[huffman_index], huffman_bits[huffman_index]);
                write_to_bit_buffer(compressor, match_index, compressor->conf_window);
            }
        }
    } else
#endif
    {
        // Non-lazy matching path
        if (TAMP_UNLIKELY(match_size < compressor->min_pattern_size)) {
            // Write LITERAL
            match_size = 1;
            unsigned char c = read_input(0);
            if (TAMP_UNLIKELY(c >> compressor->conf_literal)) {
                return TAMP_EXCESS_BITS;
            }
            write_to_bit_buffer(compressor, IS_LITERAL_FLAG | c, compressor->conf_literal + 1);

            if (compressor->conf_v2) {
                compressor->write_state = TAMP_WRITE_STATE_NORMAL;
            }
        } else {
            // Write TOKEN

#if TAMP_EXTENDED_MATCH
            // V2: Use extended match for large matches
            if (compressor->conf_v2 && match_size > (compressor->min_pattern_size + 11)) {
                // Store extended match info and enter extension mode
                compressor->extended_match_position = match_index;
                compressor->count = match_size;

                // Consume the initial match from input buffer
                for (uint8_t i = 0; i < match_size; i++) {
                    compressor->input_pos = input_add(1);
                }
                compressor->input_size -= match_size;

                // Enter extension mode - will try to extend on next poll
                compressor->write_state = TAMP_WRITE_STATE_EXTENDING_MATCH;

                return TAMP_OK;
            } else
#endif
            {
                // Regular v1 match or small v2 match
                uint8_t huffman_index = match_size - compressor->min_pattern_size;
                write_to_bit_buffer(compressor, huffman_codes[huffman_index], huffman_bits[huffman_index]);
                write_to_bit_buffer(compressor, match_index, compressor->conf_window);
            }
        }
    }
    // Populate Window
    for (uint8_t i = 0; i < match_size; i++) {
        compressor->window[compressor->window_pos] = read_input(0);
        compressor->window_pos = (compressor->window_pos + 1) & window_mask;
        compressor->input_pos = input_add(1);
    }
    compressor->input_size -= match_size;

    if (compressor->conf_v2 && match_size > 0) {
        compressor->write_state = TAMP_WRITE_STATE_NORMAL;
    }

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

#if TAMP_EXTENDED_MATCH
    // Finalize any pending extended match (v2)
    if (compressor->conf_v2 && compressor->write_state == TAMP_WRITE_STATE_EXTENDING_MATCH) {
        const uint16_t window_mask = (1 << compressor->conf_window) - 1;

        // Write extended match token
        write_to_bit_buffer(compressor, huffman_codes[EXTENDED_MATCH_SYMBOL], huffman_bits[EXTENDED_MATCH_SYMBOL]);
        write_to_bit_buffer(compressor, compressor->extended_match_position, compressor->conf_window);

        // Write extended huffman encoded match size
        uint32_t encoded_size = compressor->count - compressor->min_pattern_size - 11 - 1;
        uint8_t temp_bit_pos = compressor->bit_buffer_pos;
        tamp_write_extended_huffman(&compressor->bit_buffer, &temp_bit_pos, encoded_size,
                                    LEADING_EXTENDED_MATCH_HUFFMAN_BITS);
        compressor->bit_buffer_pos = temp_bit_pos;

        // Copy from window to window
        for (uint16_t i = 0; i < compressor->count; i++) {
            uint16_t src_pos = (compressor->extended_match_position + i) & window_mask;
            compressor->window[compressor->window_pos] = compressor->window[src_pos];
            compressor->window_pos = (compressor->window_pos + 1) & window_mask;
        }

        // Reset state
        compressor->count = 0;
        compressor->extended_match_position = 0;
        compressor->write_state = TAMP_WRITE_STATE_NORMAL;
    }
#endif

    // Flush any pending RLE at end of stream (v2)
    if (compressor->conf_v2 && compressor->count > 0) {
        write_rle(compressor);

        // Partial flush to output the RLE token
        res = partial_flush(compressor, output, output_size, &chunk_output_written_size);
        output_size -= chunk_output_written_size;
        (*output_written_size) += chunk_output_written_size;
        output += chunk_output_written_size;
        if (TAMP_UNLIKELY(res != TAMP_OK)) return res;
    }

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
