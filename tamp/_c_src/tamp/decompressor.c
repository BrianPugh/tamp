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
 * @brief Copy pattern from window to window, updating window_pos.
 *
 * Handles potential overlap between source and destination regions by
 * using a temporary buffer when necessary. Overlap occurs when the
 * destination would "catch up" to the source during copying.
 */
static inline void window_copy(unsigned char *window, uint16_t *window_pos, uint16_t window_offset, uint8_t match_size,
                               uint16_t window_mask) {
    const uint16_t src_to_dst = (*window_pos - window_offset) & window_mask;
    const bool overlap = (src_to_dst < match_size) && (src_to_dst > 0);

    if (TAMP_UNLIKELY(overlap)) {
        uint8_t tmp_buf[16];
        for (uint8_t i = 0; i < match_size; i++) {
            tmp_buf[i] = window[window_offset + i];
        }
        for (uint8_t i = 0; i < match_size; i++) {
            window[*window_pos] = tmp_buf[i];
            *window_pos = (*window_pos + 1) & window_mask;
        }
    } else {
        for (uint8_t i = 0; i < match_size; i++) {
            window[*window_pos] = window[window_offset + i];
            *window_pos = (*window_pos + 1) & window_mask;
        }
    }
}

tamp_res tamp_decompressor_read_header(TampConf *conf, const unsigned char *input, size_t input_size,
                                       size_t *input_consumed_size) {
    if (input_consumed_size) (*input_consumed_size) = 0;
    if (input_size == 0) return TAMP_INPUT_EXHAUSTED;
    if (input[0] & 0x2) return TAMP_INVALID_CONF;  // Reserved
    if (input[0] & 0x1) return TAMP_INVALID_CONF;  // Currently only a single header byte is supported.
    if (input_consumed_size) (*input_consumed_size)++;

    conf->window = ((input[0] >> 5) & 0x7) + 8;
    conf->literal = ((input[0] >> 3) & 0x3) + 5;
    conf->use_custom_dictionary = ((input[0] >> 2) & 0x1);

    return TAMP_OK;
}

/**
 * Populate the rest of the decompressor structure after the following fields have been populated:
 *   * window
 *   * window_bits_max
 */
static tamp_res tamp_decompressor_populate_from_conf(TampDecompressor *decompressor, uint8_t conf_window,
                                                     uint8_t conf_literal, uint8_t conf_use_custom_dictionary) {
    if (conf_window < 8 || conf_window > 15) return TAMP_INVALID_CONF;
    if (conf_literal < 5 || conf_literal > 8) return TAMP_INVALID_CONF;
    if (conf_window > decompressor->window_bits_max) return TAMP_INVALID_CONF;
    if (!conf_use_custom_dictionary) tamp_initialize_dictionary(decompressor->window, (size_t)1 << conf_window);

    decompressor->conf_window = conf_window;
    decompressor->conf_literal = conf_literal;
    decompressor->min_pattern_size = tamp_compute_min_pattern_size(conf_window, conf_literal);
    decompressor->configured = true;

    return TAMP_OK;
}

tamp_res tamp_decompressor_init(TampDecompressor *decompressor, const TampConf *conf, unsigned char *window,
                                uint8_t window_bits) {
    tamp_res res = TAMP_OK;

    // Validate window_bits parameter
    if (window_bits < 8 || window_bits > 15) return TAMP_INVALID_CONF;

    for (uint8_t i = 0; i < sizeof(TampDecompressor); i++)  // Zero-out the struct
        ((unsigned char *)decompressor)[i] = 0;
    decompressor->window = window;
    decompressor->window_bits_max = window_bits;
    if (conf) {
        res = tamp_decompressor_populate_from_conf(decompressor, conf->window, conf->literal,
                                                   conf->use_custom_dictionary);
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

        res = tamp_decompressor_populate_from_conf(decompressor, conf.window, conf.literal, conf.use_custom_dictionary);
        if (res != TAMP_OK) return res;

        input += header_consumed_size;
        (*input_consumed_size) += header_consumed_size;
    }

    // Cache bitfield values in local variables for faster access
    const uint8_t conf_window = decompressor->conf_window;
    const uint8_t conf_literal = decompressor->conf_literal;
    const uint8_t min_pattern_size = decompressor->min_pattern_size;

    const uint16_t window_mask = (1 << conf_window) - 1;
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
            if (TAMP_UNLIKELY(decompressor->bit_buffer_pos < (1 + conf_literal))) return TAMP_INPUT_EXHAUSTED;
            decompressor->bit_buffer <<= 1;  // shift out the is_literal flag

            // Copy literal to output
            *output = decompressor->bit_buffer >> (32 - conf_literal);
            decompressor->bit_buffer <<= conf_literal;
            decompressor->bit_buffer_pos -= (1 + conf_literal);

            // Update window
            decompressor->window[decompressor->window_pos] = *output;
            decompressor->window_pos = (decompressor->window_pos + 1) & window_mask;

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
            if (TAMP_UNLIKELY(bit_buffer_pos < conf_window)) {
                // There are not enough bits to decode window offset
                return TAMP_INPUT_EXHAUSTED;
            }
            match_size += min_pattern_size;
            window_offset = bit_buffer >> (32 - conf_window);

            // Security check: validate that the pattern reference (offset + size) does not
            // exceed window bounds. Malicious compressed data could craft out-of-bounds
            // references to read past the window buffer, potentially leaking memory.
            // Cast to uint32_t prevents signed integer overflow.
            const uint32_t window_size = (1u << conf_window);
            if (TAMP_UNLIKELY((uint32_t)window_offset >= window_size ||
                              (uint32_t)window_offset + (uint32_t)match_size > window_size)) {
                return TAMP_OOB;
            }

            // Apply skip_bytes
            match_size_skip = match_size - decompressor->skip_bytes;
            window_offset_skip = window_offset + decompressor->skip_bytes;

            // Check if we are output-buffer-limited, and if so to set skip_bytes.
            // Next tamp_decompressor_decompress_cb we will re-decode the same
            // token, and skip the first skip_bytes of it.
            // Otherwise, update the decompressor buffers
            size_t remaining = output_end - output;
            if (TAMP_UNLIKELY((uint8_t)match_size_skip > remaining)) {
                decompressor->skip_bytes += remaining;
                match_size_skip = remaining;
            } else {
                decompressor->skip_bytes = 0;
                decompressor->bit_buffer = bit_buffer << conf_window;
                decompressor->bit_buffer_pos = bit_buffer_pos - conf_window;
            }

            // Copy pattern to output
            for (uint8_t i = 0; i < match_size_skip; i++) {
                *output++ = decompressor->window[window_offset_skip + i];
            }
            (*output_written_size) += match_size_skip;

            if (TAMP_LIKELY(decompressor->skip_bytes == 0)) {
                uint16_t wp = decompressor->window_pos;
                window_copy(decompressor->window, &wp, window_offset, match_size, window_mask);
                decompressor->window_pos = wp;
            }
        }
        if (TAMP_UNLIKELY(callback && (res = callback(user_data, *output_written_size, input_size))))
            return (tamp_res)res;
    }
    return TAMP_INPUT_EXHAUSTED;
}

#if TAMP_STREAM

tamp_res tamp_decompress_stream(TampDecompressor *decompressor, tamp_read_t read_cb, void *read_handle,
                                tamp_write_t write_cb, void *write_handle, size_t *input_consumed_size,
                                size_t *output_written_size, tamp_callback_t callback, void *user_data) {
    size_t input_consumed_size_proxy, output_written_size_proxy;
    if (!input_consumed_size) input_consumed_size = &input_consumed_size_proxy;
    if (!output_written_size) output_written_size = &output_written_size_proxy;
    *input_consumed_size = 0;
    *output_written_size = 0;

    unsigned char input_buffer[TAMP_STREAM_WORK_BUFFER_SIZE / 2];
    unsigned char output_buffer[TAMP_STREAM_WORK_BUFFER_SIZE / 2];
    const size_t input_buffer_size = sizeof(input_buffer);
    const size_t output_buffer_size = sizeof(output_buffer);

    size_t input_pos = 0;
    size_t input_available = 0;
    bool eof_reached = false;

    while (1) {
        if (input_available == 0 && !eof_reached) {
            int bytes_read = read_cb(read_handle, input_buffer, input_buffer_size);
            if (TAMP_UNLIKELY(bytes_read < 0)) return TAMP_READ_ERROR;
            eof_reached = (bytes_read == 0);
            input_pos = 0;
            input_available = bytes_read;
            *input_consumed_size += bytes_read;
        }

        size_t chunk_consumed, chunk_written;

        tamp_res res = tamp_decompressor_decompress(decompressor, output_buffer, output_buffer_size, &chunk_written,
                                                    input_buffer + input_pos, input_available, &chunk_consumed);
        if (TAMP_UNLIKELY(res < TAMP_OK)) return res;

        input_pos += chunk_consumed;
        input_available -= chunk_consumed;

        if (TAMP_LIKELY(chunk_written > 0)) {
            int bytes_written = write_cb(write_handle, output_buffer, chunk_written);
            if (TAMP_UNLIKELY(bytes_written < 0 || (size_t)bytes_written != chunk_written)) {
                return TAMP_WRITE_ERROR;
            }
            *output_written_size += chunk_written;
        }

        if (TAMP_UNLIKELY(res == TAMP_INPUT_EXHAUSTED && eof_reached)) break;

        if (TAMP_UNLIKELY(callback)) {
            int cb_res = callback(user_data, *output_written_size, 0);
            if (TAMP_UNLIKELY(cb_res)) return (tamp_res)cb_res;
        }
    }

    return TAMP_OK;
}

#endif /* TAMP_STREAM */
