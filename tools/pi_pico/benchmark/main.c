#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"

#include "tamp/compressor.h"
#include "tamp/decompressor.h"

#include "enwik8.h"
#include "enwik8_compressed.h"

static unsigned char window_buffer[1024];
static unsigned char output_buffer[100 << 10];


int benchmark_compressor(){
    size_t compressed_size = 0;

    TampCompressor compressor;
    TampConf compressor_conf = {.literal=8, .window=10, .use_custom_dictionary=false};

    if(TAMP_OK != tamp_compressor_init(&compressor, &compressor_conf, window_buffer))
        return -1;

    printf("Beginning compressing...\n");
    if(TAMP_OK != tamp_compressor_compress_and_flush(
                    &compressor,
                    output_buffer,
                    sizeof(output_buffer),
                    &compressed_size,
                    ENWIK8,
                    sizeof(ENWIK8),
                    NULL,
                    false
                    ))
        return -2;
    return compressed_size;
}

int benchmark_decompressor(){
    TampDecompressor decompressor;
    int output_written_size;

    if(TAMP_OK != tamp_decompressor_init(&decompressor, NULL, window_buffer))
        return -1;

    if(0 > tamp_decompressor_decompress(
        &decompressor,
        output_buffer,
        sizeof(output_buffer),
        &output_written_size,
        ENWIK8_COMPRESSED,
        sizeof(ENWIK8_COMPRESSED),
        NULL
        ))
        return -2;

    return output_written_size;
}


int main(){
    //Initialise I/O
    stdio_init_all();

    while(true){
        int output_size;
        absolute_time_t start_time, end_time;

        {
            start_time = get_absolute_time();
            output_size = benchmark_compressor();
            end_time = get_absolute_time();

            printf("compression: %lld us\n", absolute_time_diff_us(start_time, end_time));
        }
        if(output_size != sizeof(ENWIK8_COMPRESSED))
            printf("Unexpected compressed size: %d\n", output_size);

        {
            start_time = get_absolute_time();
            output_size = benchmark_decompressor();
            end_time = get_absolute_time();

            printf("decompression: %lld us\n", absolute_time_diff_us(start_time, end_time));
        }
        if(output_size != sizeof(ENWIK8))
            printf("Unexpected decompressed size: %d\n", output_size);

    }
}
