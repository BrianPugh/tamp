#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define EXCESS_BITS_ERROR -1

#define MAX_PATTERN_SIZE (compressor->min_pattern_size + 13)
#define window_size (1 << compressor->conf.window)
#define window_add(offset) (\
            (compressor->window_pos + offset) % window_size\
        )
#define input_add(offset) (\
            (compressor->input_pos + offset) % sizeof(compressor->input)\
        )
#define read_input(offset) ( \
        compressor->input[input_add(offset)] \
        )
#define IS_LITERAL_FLAG (1 << compressor->conf->literal)

typedef struct TampCompressor {
    const TampConf conf;
    char *window;
    char input[16];
    uint32_t bit_buffer;
    uint32_t bit_buffer_pos:5;
    uint32_t min_pattern_size:2;
    uint32_t input_size:5;
    uint32_t input_pos:4;
    uint32_t window_pos:15;
} TampCompressor;

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

static inline void write_to_input_buffer(TampCompressor *compressor, char byte){
    compressor->input[compressor->input_pos] = byte;
    compressor->input_pos = input_add(1);
}

static inline int partial_flush(TampCompressor *compressor, char *output){
    int bytes_written=0;
    while(compressor->bit_buffer_pos >= 8){
        output[bytes_written]= compressor->bit_buffer >> 24;
        compressor->bit_buffer <<= 8;
        bytes_written++;
    }
    return bytes_written;
}


int tamp_compressor_init(TampCompressor *compressor, const TampConf *conf){
    // TODO
    compressor->min_pattern_size = compute_min_pattern_size(conf->window, conf->literal);

    compressor->input_size = 0;
    compressor->input_pos = 0;
}

/**
 * @brief Run a single compression iteration on the internal input buffer.
 */
int tamp_compressor_compress_input(TampCompressor *compressor, char *output){
    uint8_t best_match_size = 0;
    uint16_t best_match_index = 0;

    if(compressor->input_size == 0){
        return 0;
    }

find_best_match:
    for(uint16_t window_index=0; window_index < window_size; window_index++){
        for(uint8_t input_offset=0; input_offset < compressor->input_size; input_offset++){
            char c = read_input(input_offset);
            if(compressor->window[window_index] != c){
                break;
            }
            if(input_offset > best_match_size){
                best_match_size = input_offset;
                best_match_index = window_index;
                if(best_match_size == MAX_PATTERN_SIZE){
                    goto write_symbol;
                }
            }
        }
    }

write_symbol:
    if(best_match_size < compressor->min_pattern_size){
        // Write LITERAL
        best_match_size = 1;
        char c = read_input(0);
        if(c >> compressor->conf->literal){
            return EXCESS_BITS_ERROR;
        }
        write_to_bit_buffer(
                compressor,
                IS_LITERAL_FLAG | c,
                compressor->conf->literal + 1
                );
    }
    else{
        // Write TOKEN
        uint8_t huffman_index = best_match_size - compressor->min_pattern_size;
        write_to_bit_buffer(
                compressor,
                huffman_codes[huffman_index],
                huffman_bits[huffman_index]
                );
    }
populate_window:
    for(uint8_t i=0; i < best_match_size; i++){
        compressor->window[compressor->window_pos] = read_input(0);
        compressor->window_pos = window_add(1);
        compressor->input_pos = input_add(1);
        compressor->input_size--;
    }

write_output:
    return partial_flush(compressor, output);
}

int tamp_compressor_compress(
        TampCompressor *compressor,
        char *output,
        size_t output_size,
        const char *input,
        size_t input_size
        ){
    int bytes_written = 0;
    for(size_t i=0; i < input_size; i++){
        write_to_input_buffer(compressor, input[i]);

        if(compressor->input_size == sizeof(compressor->input)){
            // Input buffer is full and ready to start compressing.
            int chunk_compressed_size = tamp_compressor_compress_input(compressor);
            if (chunk_compressed_size < 0){
                // TODO
            }
            bytes_written += chunk_compressed_size;
        }
    }
    return bytes_written;
}

int tamp_compressor_flush(TampCompressor *compressor, bool write_token){
    if
    // TODO
}

/**
 * @brief Single-shot compress data.
 *
 * @param output Pointer to a pre-allocated buffer to hold the output compressed data.
 * @param output_size Size of the pre-allocated buffer. Will decompress up-to this many bytes.
 * @param input Pointer to the input data to be compressed.
 * @param input_size Number of bytes in input data.
 * @param conf Tamp configuration. If NULL, will use defaults.
 *
 * @return Number of bytes in the compressed output. Negative value for error code.
 * */
int tamp_compress(char *output, size_t output_size, const char *input, size_t input_size, const TampConf *conf){
    if( conf == NULL){
        // TODO: set defaults
    }
    // TODO
}
