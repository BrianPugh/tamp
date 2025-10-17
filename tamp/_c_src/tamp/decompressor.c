#include "decompressor.h"

#include "common.h"

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define FLUSH 15

/**
 * This array was generated with tools/huffman_jump_table.py
 *
 * The idea is that the resulting code is smaller/faster as a lookup table than a bunch of if/else
 * statements.
 *
 * Of each element:
 *  * The upper 4 bits express the number of bits to decode.
 *  * The lower 4 bits express the decoded value, with FLUSH being represented as 0b1111
 */
static const uint8_t HUFFMAN_TABLE[128] = {
    50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50,  50,  85,  85,  85, 85, 122, 123, 104, 104, 86, 86,
    86, 86, 93, 93, 93, 93, 68, 68, 68, 68, 68, 68, 68, 68, 105, 105, 124, 127, 87, 87, 87,  87,  51,  51,  51, 51,
    51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 17, 17, 17,  17,  17,  17,  17, 17, 17,  17,  17,  17,  17, 17,
    17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,  17,  17,  17,  17, 17, 17,  17,  17,  17,  17, 17,
    17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,  17,  17,  17,  17, 17, 17,  17,  17,  17};

/**
 * @brief Decode a huffman match-size symbol from the decompressor's bit_buffer.
 *
 * Internally updates bit_buffer and bit_buffer_pos.
 *
 * bit_buffer MUST have at least 8 bits prior to calling.
 *
 * @returns Decoded match_size
 */
static inline int8_t huffman_decode(uint32_t *bit_buffer, uint8_t *bit_buffer_pos) {
    uint8_t code;
    uint8_t bit_len;

    (*bit_buffer_pos)--;
    code = *bit_buffer >> 31;
    *bit_buffer <<= 1;
    if (TAMP_LIKELY(code == 0)) return 0;

    code = *bit_buffer >> (32 - 7);
    code = HUFFMAN_TABLE[code];
    bit_len = code >> 4;
    *bit_buffer <<= bit_len;
    (*bit_buffer_pos) -= bit_len;

    return code & 0xF;
}

/**
 * @brief Read extended Huffman encoded value from bit stream.
 *
 * Used for v2 RLE and extended match decoding.
 *
 * @param[in] input Pointer to input data.
 * @param[in,out] input_size Pointer to remaining input size.
 * @param[in,out] bit_buffer Pointer to the bit buffer.
 * @param[in,out] bit_buffer_pos Pointer to current bit position in buffer.
 * @param[in] leading_bits Number of leading bits (3 for extended match, 4 for RLE).
 * @param[out] value Decoded value.
 *
 * @return Tamp Status Code.
 */
static tamp_res tamp_read_extended_huffman(const unsigned char **input, size_t *input_size, uint32_t *bit_buffer,
                                           uint8_t *bit_buffer_pos, uint8_t leading_bits, uint32_t *value) {
    // Fill bit buffer
    while (*bit_buffer_pos < 16 && *input_size > 0) {
        *bit_buffer |= ((uint32_t)(**input)) << (24 - *bit_buffer_pos);
        (*input)++;
        (*input_size)--;
        *bit_buffer_pos += 8;
    }

    if (*bit_buffer_pos < 8) {
        return TAMP_INPUT_EXHAUSTED;
    }

    // Decode the huffman code using the existing huffman_decode function
    int8_t code_index = huffman_decode(bit_buffer, bit_buffer_pos);
    if (code_index < 0 || code_index >= 14) {
        return TAMP_ERROR;
    }

    // Fill bit buffer for leading bits
    while (*bit_buffer_pos < leading_bits && *input_size > 0) {
        *bit_buffer |= ((uint32_t)(**input)) << (24 - *bit_buffer_pos);
        (*input)++;
        (*input_size)--;
        *bit_buffer_pos += 8;
    }

    if (*bit_buffer_pos < leading_bits) {
        return TAMP_INPUT_EXHAUSTED;
    }

    // Read the lower bits
    uint8_t lower_bits = (*bit_buffer >> (32 - leading_bits)) & ((1 << leading_bits) - 1);
    *bit_buffer <<= leading_bits;
    *bit_buffer_pos -= leading_bits;

    *value = (code_index << leading_bits) | lower_bits;

    return TAMP_OK;
}

tamp_res tamp_decompressor_read_header(TampConf *conf, const unsigned char *input, size_t input_size,
                                       size_t *input_consumed_size) {
    if (input_consumed_size) (*input_consumed_size) = 0;
    if (input_size == 0) return TAMP_INPUT_EXHAUSTED;
    if (input[0] & 0x1) return TAMP_INVALID_CONF;  // Currently only a single header byte is supported.
    if (input_consumed_size) (*input_consumed_size)++;

    conf->window = ((input[0] >> 5) & 0x7) + 8;
    conf->literal = ((input[0] >> 3) & 0x3) + 5;
    conf->use_custom_dictionary = ((input[0] >> 2) & 0x1);
    conf->v2 = ((input[0] >> 1) & 0x1);

    return TAMP_OK;
}

/**
 * Populate the rest of the decompressor structure after the following fields have been populated:
 *   * conf
 *   * window
 */
static tamp_res tamp_decompressor_populate_from_conf(TampDecompressor *decompressor, uint8_t conf_window,
                                                     uint8_t conf_literal, uint8_t conf_use_custom_dictionary,
                                                     uint8_t conf_v2) {
    if (conf_window < 8 || conf_window > 15) return TAMP_INVALID_CONF;
    if (conf_literal < 5 || conf_literal > 8) return TAMP_INVALID_CONF;
    if (!conf_use_custom_dictionary) tamp_initialize_dictionary(decompressor->window, (1 << conf_window));

    decompressor->conf_window = conf_window;
    decompressor->conf_literal = conf_literal;
    decompressor->conf_v2 = conf_v2;

    decompressor->min_pattern_size = tamp_compute_min_pattern_size(conf_window, conf_literal);
    decompressor->configured = true;

    // Initialize v2 fields
    if (conf_v2) {
        decompressor->rle_last_written = 0;
        decompressor->last_written_byte = 0;
    }

    return TAMP_OK;
}

tamp_res tamp_decompressor_init(TampDecompressor *decompressor, const TampConf *conf, unsigned char *window) {
    tamp_res res = TAMP_OK;
    for (uint8_t i = 0; i < sizeof(TampDecompressor); i++)  // Zero-out the struct
        ((unsigned char *)decompressor)[i] = 0;
    decompressor->window = window;
    if (conf) {
        res = tamp_decompressor_populate_from_conf(decompressor, conf->window, conf->literal,
                                                   conf->use_custom_dictionary, conf->v2);
    }

    return res;
}

tamp_res tamp_decompressor_decompress_cb(TampDecompressor *decompressor, unsigned char *output, size_t output_size,
                                         size_t *output_written_size, const unsigned char *input, size_t input_size,
                                         size_t *input_consumed_size, tamp_callback_t callback, void *user_data) {
    size_t input_consumed_size_proxy;
    size_t output_written_size_proxy;
    tamp_res res;
    const unsigned char *input_end = input + input_size;
    const unsigned char *output_end = output + output_size;

    if (!output_written_size) output_written_size = &output_written_size_proxy;
    if (!input_consumed_size) input_consumed_size = &input_consumed_size_proxy;

    *input_consumed_size = 0;
    *output_written_size = 0;

    if (!decompressor->configured) {
        // Read in header
        size_t header_consumed_size;
        TampConf conf;
        res = tamp_decompressor_read_header(&conf, input, input_end - input, &header_consumed_size);
        if (res != TAMP_OK) return res;

        res = tamp_decompressor_populate_from_conf(decompressor, conf.window, conf.literal, conf.use_custom_dictionary,
                                                   conf.v2);
        if (res != TAMP_OK) return res;

        input += header_consumed_size;
        (*input_consumed_size) += header_consumed_size;
    }
    const uint16_t window_mask = (1 << decompressor->conf_window) - 1;
    while (input != input_end || decompressor->bit_buffer_pos) {
        // Populate the bit buffer
        while (input != input_end && decompressor->bit_buffer_pos <= 24) {
            uint32_t t = *input;
            decompressor->bit_buffer_pos += 8;
            decompressor->bit_buffer |= t << (32 - decompressor->bit_buffer_pos);
            input++;
            (*input_consumed_size)++;
        }

        if (TAMP_UNLIKELY(decompressor->bit_buffer_pos == 0)) return TAMP_INPUT_EXHAUSTED;

        if (TAMP_UNLIKELY(output == output_end)) return TAMP_OUTPUT_FULL;

        // Hint that patterns are more likely than literals
        if (TAMP_UNLIKELY(decompressor->bit_buffer >> 31)) {
            // is literal
            if (TAMP_UNLIKELY(decompressor->bit_buffer_pos < (1 + decompressor->conf_literal)))
                return TAMP_INPUT_EXHAUSTED;
            decompressor->bit_buffer <<= 1;  // shift out the is_literal flag

            // Copy literal to output
            *output = decompressor->bit_buffer >> (32 - decompressor->conf_literal);
            decompressor->bit_buffer <<= decompressor->conf_literal;
            decompressor->bit_buffer_pos -= (1 + decompressor->conf_literal);

            // Update window
            decompressor->window[decompressor->window_pos] = *output;
            decompressor->window_pos = (decompressor->window_pos + 1) & window_mask;

            // Update v2 state
            if (decompressor->conf_v2) {
                decompressor->last_written_byte = *output;
                decompressor->rle_last_written = 0;
            }

            output++;
            (*output_written_size)++;
        } else {
            // is token; attempt a decode
            /* copy the bit buffers so that we can abort at any time */
            uint32_t bit_buffer = decompressor->bit_buffer;
            uint16_t window_offset;
            uint16_t window_offset_skip;
            uint8_t bit_buffer_pos = decompressor->bit_buffer_pos;
            int8_t match_size;
            int8_t match_size_skip;

            // shift out the is_literal flag
            bit_buffer <<= 1;
            bit_buffer_pos--;

            // There must be at least 8 bits, otherwise no possible decoding.
            if (TAMP_UNLIKELY(bit_buffer_pos < 8)) return TAMP_INPUT_EXHAUSTED;

            match_size = huffman_decode(&bit_buffer, &bit_buffer_pos);
            if (TAMP_UNLIKELY(match_size == FLUSH)) {
                // flush bit_buffer to the nearest byte and skip the remainder of decoding
                decompressor->bit_buffer = bit_buffer << (bit_buffer_pos & 7);
                decompressor->bit_buffer_pos =
                    bit_buffer_pos & ~7;  // Round bit_buffer_pos down to nearest multiple of 8.
                continue;
            }

            // Handle v2 RLE (symbol 12)
            if (decompressor->conf_v2 && TAMP_UNLIKELY(match_size == RLE_SYMBOL)) {
                uint32_t rle_value;
                size_t temp_input_size = input_end - input;
                const unsigned char *temp_input = input;
                res = tamp_read_extended_huffman(&temp_input, &temp_input_size, &bit_buffer, &bit_buffer_pos,
                                                 LEADING_RLE_HUFFMAN_BITS, &rle_value);
                if (res != TAMP_OK) return res;

                // Update input consumption
                size_t consumed = (input_end - input) - temp_input_size;
                input += consumed;
                (*input_consumed_size) += consumed;

                uint16_t rle_count = rle_value + 2;
                unsigned char rle_byte = decompressor->last_written_byte;

                // Write RLE bytes to output
                size_t remaining = output_end - output;
                uint16_t bytes_to_output = MIN(rle_count, remaining);
                for (uint16_t i = 0; i < bytes_to_output; i++) {
                    *output++ = rle_byte;
                }
                (*output_written_size) += bytes_to_output;

                // Update window if not already written from previous RLE
                if (!decompressor->rle_last_written) {
                    uint16_t bytes_to_window = MIN(rle_count, RLE_MAX_WINDOW);
                    for (uint16_t i = 0; i < bytes_to_window; i++) {
                        decompressor->window[decompressor->window_pos] = rle_byte;
                        decompressor->window_pos = (decompressor->window_pos + 1) & window_mask;
                    }
                }

                decompressor->rle_last_written = 1;
                decompressor->bit_buffer = bit_buffer;
                decompressor->bit_buffer_pos = bit_buffer_pos;

                if (bytes_to_output < rle_count) {
                    return TAMP_OUTPUT_FULL;
                }
                continue;
            }

            // Handle v2 Extended Match (symbol 13)
            if (decompressor->conf_v2 && TAMP_UNLIKELY(match_size == EXTENDED_MATCH_SYMBOL)) {
                if (TAMP_UNLIKELY(bit_buffer_pos < decompressor->conf_window)) {
                    return TAMP_INPUT_EXHAUSTED;
                }
                window_offset = bit_buffer >> (32 - decompressor->conf_window);
                bit_buffer <<= decompressor->conf_window;
                bit_buffer_pos -= decompressor->conf_window;

                uint32_t extended_value;
                size_t temp_input_size = input_end - input;
                const unsigned char *temp_input = input;
                res = tamp_read_extended_huffman(&temp_input, &temp_input_size, &bit_buffer, &bit_buffer_pos,
                                                 LEADING_EXTENDED_MATCH_HUFFMAN_BITS, &extended_value);
                if (res != TAMP_OK) return res;

                // Update input consumption
                size_t consumed = (input_end - input) - temp_input_size;
                input += consumed;
                (*input_consumed_size) += consumed;

                match_size = extended_value + decompressor->min_pattern_size + 11 + 1;

                // Copy extended match to output and window
                size_t remaining = output_end - output;
                uint16_t bytes_to_output = MIN((size_t)match_size, remaining);
                for (uint16_t i = 0; i < bytes_to_output; i++) {
                    unsigned char byte_val = decompressor->window[(window_offset + i) & window_mask];
                    *output++ = byte_val;
                    decompressor->window[decompressor->window_pos] = byte_val;
                    decompressor->last_written_byte = byte_val;
                    decompressor->window_pos = (decompressor->window_pos + 1) & window_mask;
                }
                (*output_written_size) += bytes_to_output;

                decompressor->rle_last_written = 0;
                decompressor->bit_buffer = bit_buffer;
                decompressor->bit_buffer_pos = bit_buffer_pos;

                if (bytes_to_output < match_size) {
                    return TAMP_OUTPUT_FULL;
                }
                continue;
            }

            if (TAMP_UNLIKELY(bit_buffer_pos < decompressor->conf_window)) {
                // There are not enough bits to decode window offset
                return TAMP_INPUT_EXHAUSTED;
            }
            match_size += decompressor->min_pattern_size;
            window_offset = bit_buffer >> (32 - decompressor->conf_window);

            // Apply skip_bytes
            match_size_skip = match_size - decompressor->skip_bytes;
            window_offset_skip = window_offset + decompressor->skip_bytes;

            // Check if we are output-buffer-limited, and if so to set skip_bytes
            // Otherwise, update the decompressor buffers
            size_t remaining = output_end - output;
            if (TAMP_UNLIKELY((uint8_t)match_size_skip > remaining)) {
                decompressor->skip_bytes += remaining;
                match_size_skip = remaining;
            } else {
                decompressor->skip_bytes = 0;
                decompressor->bit_buffer = bit_buffer << decompressor->conf_window;
                decompressor->bit_buffer_pos = bit_buffer_pos - decompressor->conf_window;
            }

            // Copy pattern to output
            for (uint8_t i = 0; i < match_size_skip; i++) {
                *output++ = decompressor->window[window_offset_skip + i];
            }
            (*output_written_size) += match_size_skip;

            if (TAMP_LIKELY(decompressor->skip_bytes == 0)) {
                // Perform window update;
                // copy to a temporary buffer in case src precedes dst, and is overlapping.
                uint8_t tmp_buf[16];
                for (uint8_t i = 0; i < match_size; i++) {
                    tmp_buf[i] = decompressor->window[window_offset + i];
                }
                for (uint8_t i = 0; i < match_size; i++) {
                    decompressor->window[decompressor->window_pos] = tmp_buf[i];
                    decompressor->window_pos = (decompressor->window_pos + 1) & window_mask;
                }

                // Update v2 state
                if (decompressor->conf_v2 && match_size > 0) {
                    decompressor->last_written_byte = tmp_buf[match_size - 1];
                    decompressor->rle_last_written = 0;
                }
            }
        }
        if (TAMP_UNLIKELY(callback && (res = callback(user_data, *output_written_size, input_size))))
            return (tamp_res)res;
    }
    return TAMP_INPUT_EXHAUSTED;
}
