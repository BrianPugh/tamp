#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "tamp/decompressor.h"
#include "tamp/compressor.h"

#define EXIT(x, fmt, ...) { res=x; fprintf(stderr, fmt, ##__VA_ARGS__); goto exit; }

int benchmark_compressor(){
    int res = 0;
    FILE *uncompressed_file = NULL;
    unsigned char *uncompressed_data = NULL;
    FILE *compressed_file = NULL;
    unsigned char *compressed_data = NULL;
    size_t uncompressed_file_len;
    size_t compressed_len=0;

    TampCompressor compressor;
    TampConf compressor_conf = {.literal=8, .window=10, .use_custom_dictionary=false};
    unsigned char *compressor_buffer = NULL;

    if (!(compressor_buffer = malloc(1 << compressor_conf.window)))
        EXIT(1, "OOM");

    if (!(uncompressed_file = fopen("../../build/enwik8", "rb")))
        EXIT(1, "Unable to open uncompressed_file %s", "../../build/enwik8");
    if (!(compressed_file = fopen("output.tamp", "wb")))
        EXIT(1, "Unable to open compressed_file");

    fseek(uncompressed_file, 0, SEEK_END);
    uncompressed_file_len = ftell(uncompressed_file);
    fseek(uncompressed_file, 0, SEEK_SET);

    if (!(uncompressed_data = malloc(uncompressed_file_len)))
        EXIT(1, "OOM");

    if (!(compressed_data = malloc(uncompressed_file_len)))
        EXIT(1, "OOM");

    fread(uncompressed_data, uncompressed_file_len, 1, uncompressed_file);
    printf("Uncompressed Length: %zu\n", uncompressed_file_len);

    if(TAMP_OK != tamp_compressor_init(&compressor, &compressor_conf, compressor_buffer))
        EXIT(1, "Failed to initialize compressor");

    printf("Beginning compressing...\n");
    size_t consumed;
    if(TAMP_OK != tamp_compressor_compress_and_flush(
                &compressor,
                compressed_data,
                uncompressed_file_len,
                &compressed_len,
                uncompressed_data,
                uncompressed_file_len,
                &consumed,
                false
                ))
        EXIT(1, "Failed to compress data");

    fwrite(compressed_data, 1, compressed_len, compressed_file);

    printf("Consumed: %zu\n", consumed);
    printf("Compressed Length: %zu\n", compressed_len);

exit:
    if(uncompressed_data)
        free(uncompressed_data);
    if(compressed_data)
        free(compressed_data);
    if(compressor_buffer)
        free(compressor_buffer);
    if(uncompressed_file)
        fclose(uncompressed_file);
    if(compressed_file)
        fclose(compressed_file);

    return res;
}

int benchmark_decompressor(){
    int res = 0;

    unsigned char *input = NULL, *output = NULL, *window_buffer = NULL;
    size_t input_size = 100 << 20;
    size_t output_size = 100 << 20;
    size_t input_consumed_size, output_written_size;
    FILE *input_file = NULL, *output_file = NULL;

    TampDecompressor decompressor;

    if (!(window_buffer = malloc(32 << 10)))
        EXIT(1, "OOM");

    if (!(input = malloc(input_size)))
        EXIT(1, "OOM");

    if (!(output = malloc(output_size)))
        EXIT(1, "OOM");

    if (!(input_file = fopen("../../build/enwik8.tamp", "rb")))
        EXIT(1, "Unable to open input file %s", "../../build/enwik8.tamp");

    if (!(output_file = fopen("build/enwik8_reconstructed", "wb")))
        EXIT(1, "Unable to open output file");

    // Read in data
    fseek(input_file, 0, SEEK_END);
    input_size = ftell(input_file);
    printf("Input file size: %zu\n", input_size);
    rewind(input_file);
    assert(input_size == fread(input, 1, input_size, input_file));

    if(TAMP_OK != tamp_decompressor_init(&decompressor, NULL, window_buffer))
        EXIT(1, "Failed to initialize compressor");

    if(0 > tamp_decompressor_decompress(
        &decompressor,
        output,
        output_size,
        &output_written_size,
        input,
        input_size,
        &input_consumed_size
        ))
        EXIT(res, "Failed to decompress data");

    printf("output_written_size: %zu\n", output_written_size);
    fwrite(output, 1, output_written_size, output_file);

exit:
    if(input)
        free(input);
    if(output)
        free(output);
    if(window_buffer)
        free(window_buffer);
    if(input_file)
        fclose(input_file);
    if(output_file)
        fclose(output_file);

    return res;
}

int main(int argc, char *argv[]) {
    if(strcmp(argv[1], "compressor") == 0)
        return benchmark_compressor();
    else if(strcmp(argv[1], "decompressor") == 0)
        for(uint8_t i = 0; i < 16; i++)
            benchmark_decompressor();
    else
        printf("invalid cli argument\n");
}
