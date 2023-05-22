#include <string.h>
#include "decompressor.h"
#include "common.h"
#include <stdio.h>
#include "assert.h"

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define FLUSH 127

/**
 * @brief Decode a huffman match-size symbol from the decompressor's bit_buffer.
 *
 * Internally updates bit_buffer and bit_buffer_pos.
 * In the event of TAMP_INPUT_EXHAUSTED, bit_buffer is NOT restored.
 *
 * @return Tamp Status Code.
 *     TAMP_INVALID_SYMBOL if no valid symbol decoded. Buffer is NOT restored.
 */
static int8_t huffman_decode(TampDecompressor *decompressor){
    uint8_t code = 0;

    for(uint8_t i=0; i < 8; i++){
        if(decompressor->bit_buffer_pos == 0)
            return TAMP_INPUT_EXHAUSTED;
        code = (code << 1) | (decompressor->bit_buffer >> 31);
        decompressor->bit_buffer <<= 1;
        decompressor->bit_buffer_pos -= 1;
        switch(code){
            case 0:   return 0;
            case 3:   return 1;
            case 8:   return 2;
            case 11:  return 3;
            case 20:  return 4;
            case 36:  return 5;
            case 38:  return 6;
            case 43:  return 7;
            case 75:  return 8;
            case 84:  return 9;
            case 148: return 10;
            case 149: return 11;
            case 170: return 12;
            case 39:  return 13;
            case 171: return FLUSH;
        }
    }
    // Huffman Trees are complete, so this should never happen.
    assert(false);
    return TAMP_ERROR;  // for the compiler to not yell about return-values
}

tamp_res tamp_decompressor_read_header(TampConf *conf, const unsigned char *input, size_t input_size, size_t *input_consumed_size) {
    if(input_consumed_size)
        (*input_consumed_size) = 0;
    if(input_size == 0)
        return TAMP_INPUT_EXHAUSTED;
    if(input[0] & 0x1)
        return TAMP_INVALID_CONF;  // Currently only a single header byte is supported.
    if(input_consumed_size)
        (*input_consumed_size)++;

    conf->window = ((input[0] >> 5) & 0x7) + 8;
    conf->literal = ((input[0] >> 3) & 0x3) + 5;
    conf->use_custom_dictionary = ((input[0] >> 2) & 0x1);

    return TAMP_OK;
}

tamp_res tamp_decompressor_init(TampDecompressor *decompressor, const TampConf *conf, unsigned char *window){
    decompressor->window = window;
    decompressor->window_pos = 0;
    decompressor->bit_buffer = 0;
    decompressor->bit_buffer_pos = 0;
    decompressor->skip_bytes = 0;

    if(conf){
        if(conf->window < 8 || conf->window > 15)
            return TAMP_INVALID_CONF;
        if(conf->literal < 5 || conf->literal > 8)
            return TAMP_INVALID_CONF;
        if(!conf->use_custom_dictionary )
            tamp_initialize_dictionary(window, (1 << conf->window));

        decompressor->min_pattern_size = tamp_compute_min_pattern_size(conf->window, conf->literal);
        memcpy(&decompressor->conf, conf, sizeof(TampConf));
        decompressor->configured = true;
    }
    else{
        // Defer configuration to tamp_decompressor_decompress
        decompressor->configured = false;
    }
    return TAMP_OK;
}

tamp_res tamp_decompressor_decompress(
        TampDecompressor *decompressor,
        unsigned char *output,
        size_t output_size,
        size_t *output_written_size,
        const unsigned char *input,
        size_t input_size,
        size_t *input_consumed_size
        ){
    tamp_res res;
    const uint16_t window_mask = (1 << decompressor->conf.window) - 1;

    if(output_written_size)
        *output_written_size = 0;
    if(input_consumed_size)
        *input_consumed_size = 0;

    if(!decompressor->configured){
        //Read in header
        size_t header_consumed_size;
        res = tamp_decompressor_read_header(&decompressor->conf, input, input_size, &header_consumed_size);
        if(res != TAMP_OK)
            return res;
        input_size -= header_consumed_size;
        input++;
        if(input_consumed_size)
            (*input_consumed_size) += header_consumed_size;
    }
    while(input_size || decompressor->bit_buffer_pos){
        int8_t match_size = 1;
        // Populate the bit buffer
        while(input_size && decompressor->bit_buffer_pos <= 24){
            decompressor->bit_buffer_pos += 8;
            decompressor->bit_buffer |=  *input << (32 - decompressor->bit_buffer_pos);
            input_size--;
            input++;
            if(input_consumed_size)
                (*input_consumed_size)++;
        }

        if(output_size == 0)
            return TAMP_OUTPUT_FULL;

        if(decompressor->bit_buffer >> 31){
            // is literal
            if(decompressor->bit_buffer_pos < (1 + decompressor->conf.literal))
                return TAMP_INPUT_EXHAUSTED;
            decompressor->bit_buffer <<= 1;  // shift out the is_literal flag

            // Copy literal to output
            *output = decompressor->bit_buffer >> (32 - decompressor->conf.literal);
            decompressor->bit_buffer <<= decompressor->conf.literal;
            decompressor->bit_buffer_pos -= (1 + decompressor->conf.literal);
        }
        else{
            // is token; attempt a decode
            uint32_t bit_buffer_backup = decompressor->bit_buffer;
            uint8_t bit_buffer_pos_backup = decompressor->bit_buffer_pos;
            uint16_t window_offset;

            decompressor->bit_buffer <<= 1;  // shift out the is_literal flag
            decompressor->bit_buffer_pos--;

            match_size = huffman_decode(decompressor);
            if(match_size < 0){
                // An error occurred decoding the match-size Huffman code.
                decompressor->bit_buffer = bit_buffer_backup;
                decompressor->bit_buffer_pos = bit_buffer_pos_backup;
                return match_size;
            }
            if(match_size == FLUSH){
                // flush bit_buffer to the nearest byte and skip the remainder of decoding
                decompressor->bit_buffer <<= decompressor->bit_buffer_pos & 7;
                decompressor->bit_buffer_pos &= ~7;  // Round bit_buffer_pos down to nearest multiple of 8.
                continue;
            }
            if(decompressor->bit_buffer_pos < decompressor->conf.window){
                // There are not enough bits to decode window offset
                decompressor->bit_buffer = bit_buffer_backup;
                decompressor->bit_buffer_pos = bit_buffer_pos_backup;
                return TAMP_INPUT_EXHAUSTED;
            }
            match_size += decompressor->min_pattern_size;
            window_offset = decompressor->bit_buffer >> (32 - decompressor->conf.window);
            decompressor->bit_buffer <<= decompressor->conf.window;
            decompressor->bit_buffer_pos -= decompressor->conf.window;

            // Apply previous skip_bytes
            match_size -= decompressor->skip_bytes;
            window_offset += decompressor->skip_bytes;

            // Check if we are output-buffer-limited, and if so to set skip_bytes and restore the bit_buffer
            if((uint8_t)match_size > output_size){
                decompressor->skip_bytes += output_size;
                match_size = output_size;
                decompressor->bit_buffer = bit_buffer_backup;
                decompressor->bit_buffer_pos = bit_buffer_pos_backup;
            }
            else {
                decompressor->skip_bytes = 0;
            }

            // Copy pattern to output
            for(uint8_t i=0; i < match_size; i++){
                output[i] = decompressor->window[window_offset + i];
            }
        }
        // Copy pattern from output to current window position
        for(uint8_t i=0; i < match_size; i++){
            decompressor->window[decompressor->window_pos] = output[i];
            decompressor->window_pos = (decompressor->window_pos + 1) & window_mask;
        }

        output += match_size;
        output_size -= match_size;
        if(output_written_size)
            (*output_written_size) += match_size;

    }
    return TAMP_OK;
}
