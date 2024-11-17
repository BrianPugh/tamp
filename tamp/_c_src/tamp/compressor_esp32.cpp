#include "common.h"

/* Modification of the original tamp compressor.c, 2024 <https://github.com/BitsForPeople> */

#if (TAMP_ESP32)

#include "compressor_esp32.hpp"

using namespace tamp;

tamp_res tamp_compressor_init(TampCompressor* const compressor, const TampConf* conf, unsigned char* const window){
    return Compressor::of(compressor).init(conf, window);
}

tamp_res tamp_compressor_compress_poll(TampCompressor* const compressor, unsigned char *output, size_t output_size, size_t* const output_written_size){

    return Compressor::of(compressor).compress_poll(output, output_size, output_written_size);
}


size_t tamp_compressor_sink(
        TampCompressor* const compressor,
        const unsigned char* const input,
        size_t input_size
        ){
    return Compressor::of(compressor).sink(input,input_size);
}

tamp_res tamp_compressor_compress(
        TampCompressor* const compressor,
        unsigned char *output,
        size_t output_size,
        size_t* const output_written_size,
        const unsigned char *input,
        size_t input_size,
        size_t* const input_consumed_size
        ){


    const detail::OutArg output_written_sz {output_written_size};
    const detail::OutArg input_consumed_sz {input_consumed_size};

    output_written_sz = 0;
    input_consumed_sz = 0;

    tamp_res res = TAMP_OK;
    size_t consumed = 0;
    size_t written = 0;
    while(input_size > 0 && output_size > 0){
        {
            // Sink Data into input buffer.
            const size_t cons =
                tamp_compressor_sink(compressor, input, input_size);
            input += cons;
            input_size -= cons;
            consumed += cons;
        }
        if(compressor->input_size == Compressor::INBUF_SIZE) [[likely]] {
            // Input buffer is full and ready to start compressing.
            size_t chunk_output_written_size;
            res = tamp_compressor_compress_poll(compressor, output, output_size, &chunk_output_written_size);
            output += chunk_output_written_size;
            output_size -= chunk_output_written_size;
            // (*output_written_size) += chunk_output_written_size;
            written += chunk_output_written_size;
            if(TAMP_UNLIKELY(res != TAMP_OK)) {
                break;
            }
        }
    }
    output_written_sz = written;
    input_consumed_sz = consumed;

    return res;
}


tamp_res tamp_compressor_flush(
        TampCompressor* const compressor,
        unsigned char *output,
        size_t output_size,
        size_t *output_written_size,
        bool write_token
        ){

    return Compressor::of(compressor).flush(output, output_size, output_written_size, write_token);            
}

tamp_res tamp_compressor_compress_and_flush(
        TampCompressor *compressor,
        unsigned char *output,
        size_t output_size,
        size_t *output_written_size,
        const unsigned char *input,
        size_t input_size,
        size_t *input_consumed_size,
        bool write_token
        ){
    tamp_res res;
    size_t flush_size;
    size_t output_written_size_proxy;

    if(!output_written_size)
        output_written_size = &output_written_size_proxy;

    res = tamp_compressor_compress(
            compressor,
            output,
            output_size,
            output_written_size,
            input,
            input_size,
            input_consumed_size
            );

    if(TAMP_UNLIKELY(res != TAMP_OK))
        return res;

    res = tamp_compressor_flush(
            compressor,
            output + *output_written_size,
            output_size - *output_written_size,
            &flush_size,
            write_token
            );

    (*output_written_size) += flush_size;

    // tamp::Locator::stats.log();

    return res;
}

#endif // TAMP_ESP32