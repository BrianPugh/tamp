#include "compressor.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define MAX_PATTERN_SIZE (compressor->min_pattern_size + 13)
#define WINDOW_SIZE (1 << compressor->conf.window)
#define window_add(offset) (\
            (compressor->window_pos + offset) % WINDOW_SIZE\
        )
#define input_add(offset) (\
            (compressor->input_pos + offset) % sizeof(compressor->input)\
        )
#define read_input(offset) ( \
        compressor->input[input_add(offset)] \
        )
#define IS_LITERAL_FLAG (1 << compressor->conf.literal)

#define FLUSH_CODE (0xAB)

// encodes [min_pattern_bytes, min_pattern_bytes + 13] pattern lengths
static const char huffman_codes[] = {
    0x0, 0x3, 0x8, 0xb, 0x14, 0x24, 0x26, 0x2b, 0x4b, 0x54, 0x94, 0x95, 0xaa, 0x27
};
// These bit lengths pre-add the 1 bit for the 0-value is_literal flag.
static const uint8_t huffman_bits[] = {
    0x2, 0x3, 0x5, 0x5, 0x6, 0x7, 0x7, 0x7, 0x8, 0x8, 0x9, 0x9, 0x9, 0x7
};

static inline void write_to_bit_buffer(TampCompressor *compressor, uint32_t bits, uint8_t n_bits){
    compressor->bit_buffer_pos += n_bits;
    compressor->bit_buffer |= bits << (32 - compressor->bit_buffer_pos);
}

/**
 * @brief Partially flush the internal bit buffer.
 *
 * Up to 7 bits may remain in the internal bit buffer.
 */
static inline tamp_res partial_flush(TampCompressor *compressor, char *output, size_t output_size, size_t *output_written_size){
    if(output_written_size){
        *output_written_size = 0;
    }
    while(compressor->bit_buffer_pos >= 8 && output_size){
        *output = compressor->bit_buffer >> 24;
        output++;
        compressor->bit_buffer <<= 8;
        if(output_written_size){
            (*output_written_size)++;
        }
        output_size--;
        compressor->bit_buffer_pos -= 8;
    }
    if(compressor->bit_buffer_pos >= 8){
        return TAMP_OUTPUT_FULL;
    }
    return TAMP_OK;
}


/**
 * @brief Find the best match for the current input buffer.
 *
 * @param[in,out] compressor TampCompressor object to perform search on.
 * @param[out] match_index
 * @param[out] match_size
 */
static inline void find_best_match(
        TampCompressor *compressor,
        uint16_t *match_index,
        uint8_t *match_size
        ){
    *match_size = 0;
    for(uint16_t window_index=0; window_index < WINDOW_SIZE; window_index++){
        for(uint8_t input_offset=0; input_offset < compressor->input_size; input_offset++){
            char c = read_input(input_offset);
            if(compressor->window[window_index + input_offset] != c){
                break;
            }
            if(input_offset + 1 > *match_size){
                *match_size = input_offset + 1;
                *match_index = window_index;
                if(*match_size == MAX_PATTERN_SIZE){
                    return;
                }
            }
        }
    }
}


tamp_res tamp_compressor_init(TampCompressor *compressor, const TampConf *conf, char *window){
    const TampConf conf_default = {.window=10, .literal=8, .use_custom_dictionary=false};
    if(!conf){
        conf = &conf_default;
    }

    memcpy(&compressor->conf, conf, sizeof(TampConf));

    compressor->window = window;
    compressor->bit_buffer_pos = 0;
    compressor->min_pattern_size = compute_min_pattern_size(conf->window, conf->literal);
    compressor->input_size = 0;
    compressor->input_pos = 0;
    compressor->window_pos = 0;

    if(!compressor->conf.use_custom_dictionary){
        initialize_dictionary(window, (1 << conf->window), 3758097560);
    }

    // Write header to bit buffer
    write_to_bit_buffer(compressor, conf->window - 8, 3);
    write_to_bit_buffer(compressor, conf->literal - 5, 2);
    write_to_bit_buffer(compressor, conf->use_custom_dictionary, 1);
    write_to_bit_buffer(compressor, 0, 1);  // Reserved
    write_to_bit_buffer(compressor, 0, 1);  // No more header bytes

    return TAMP_OK;
}


tamp_res tamp_compressor_compress_poll(TampCompressor *compressor, char *output, size_t output_size, size_t *output_written_size){
    tamp_res res;

    if(output_written_size)
        *output_written_size = 0;

    if(compressor->input_size == 0)
        return TAMP_OK;

    {
        // Make sure there's enough room in the bit buffer.
        size_t flush_bytes_written;
        res = partial_flush(compressor, output, output_size, &flush_bytes_written);
        if(output_written_size){
            (*output_written_size) += flush_bytes_written;
        }
        if(res != TAMP_OK){
            return res;
        }
        output_size -= flush_bytes_written;
        output += flush_bytes_written;
    }

    if(output_size == 0){
        return TAMP_OUTPUT_FULL;
    }

    uint8_t match_size;
    uint16_t match_index;
    find_best_match(compressor, &match_index, &match_size);

    if(match_size < compressor->min_pattern_size){
        // Write LITERAL
        match_size = 1;
        char c = read_input(0);
        if(c >> compressor->conf.literal){
            return TAMP_EXCESS_BITS;
        }
        write_to_bit_buffer(
                compressor,
                IS_LITERAL_FLAG | c,
                compressor->conf.literal + 1
                );
    }
    else{
        // Write TOKEN
        uint8_t huffman_index = match_size - compressor->min_pattern_size;
        write_to_bit_buffer(
                compressor,
                huffman_codes[huffman_index],
                huffman_bits[huffman_index]
                );
    }
    // Populate Window
    for(uint8_t i=0; i < match_size; i++){
        compressor->window[compressor->window_pos] = read_input(0);
        compressor->window_pos = window_add(1);
        compressor->input_pos = input_add(1);
        compressor->input_size--;
    }

    return TAMP_OK;
}


void tamp_compressor_sink(
        TampCompressor *compressor,
        const char *input,
        size_t input_size,
        size_t *consumed_size
        ){
    if(consumed_size){
        *consumed_size = 0;
    }
    for(size_t i=0; i < input_size; i++){
        if(compressor->input_size == sizeof(compressor->input)){
            break;
        }
        compressor->input[input_add(compressor->input_size)] = input[i];
        compressor->input_size += 1;
        if(consumed_size){
            (*consumed_size)++;
        }
    }
}

tamp_res tamp_compressor_compress(
        TampCompressor *compressor,
        char *output,
        size_t output_size,
        size_t *output_written_size,
        const char *input,
        size_t input_size,
        size_t *input_consumed_size
        ){
    tamp_res res;

    if(output_written_size)
        *output_written_size = 0;
    if(input_consumed_size)
        *input_consumed_size = 0;

    while(input_size > 0 && output_size > 0){
        {
            // Sink Data into input buffer.
            size_t consumed;
            tamp_compressor_sink(compressor, input, input_size, &consumed);
            input += consumed;
            input_size -= consumed;
            if(input_consumed_size){
                (*input_consumed_size) += consumed;
            }
        }
        if(compressor->input_size == sizeof(compressor->input)){
            // Input buffer is full and ready to start compressing.
            size_t chunk_output_written_size;
            res = tamp_compressor_compress_poll(compressor, output, output_size, &chunk_output_written_size);
            output += chunk_output_written_size;
            output_size -= chunk_output_written_size;
            if(output_written_size){
                output_written_size += chunk_output_written_size;
            }
            if(res != TAMP_OK){
                return res;
            }
        }
    }
    return TAMP_OK;
}

tamp_res tamp_compressor_flush(
        TampCompressor *compressor,
        char *output,
        size_t output_size,
        size_t *output_written_size,
        bool write_token
        ){
    tamp_res res;
    size_t chunk_output_written_size;

    while(compressor->input_size){
        // Compress the remainder of the input buffer.
        res = tamp_compressor_compress_poll(compressor, output, output_size, &chunk_output_written_size);
        output_size -= chunk_output_written_size;
        if(output_written_size){
            *output_written_size += chunk_output_written_size;
        }
        output += chunk_output_written_size;
        if(res != TAMP_OK){
            return res;
        }
    }

    // Perform partial flush to see if we need a FLUSH token, and to subsequently
    // make room for the FLUSH token.
    res = partial_flush(compressor, output, output_size, &chunk_output_written_size);
    output_size -= chunk_output_written_size;
    if(output_written_size){
        *output_written_size += chunk_output_written_size;
    }
    output += chunk_output_written_size;
    if(res != TAMP_OK){
        return res;
    }

    // Maybe write the FLUSH token
    if(compressor->bit_buffer_pos && write_token){
        write_to_bit_buffer(compressor, FLUSH_CODE, 9);
    }

    // Flush the remainder of the output bit-buffer
    while(compressor->bit_buffer_pos && output_size){
        *output = compressor->bit_buffer >> 24;
        output++;
        compressor->bit_buffer <<= 8;
        compressor->bit_buffer_pos -= MIN(compressor->bit_buffer_pos, 8);
        output_size--;
        if(output_written_size){
            (*output_written_size)++;
        }
    }

    if(compressor->bit_buffer_pos){
        // There was not enough room in the output buffer to fully flush.
        return TAMP_OUTPUT_FULL;
    }

    return TAMP_OK;
}
