#include <stdio.h>
#include <stdlib.h>
#include "tamp/compressor.h"

#define EXIT(x, fmt, ...) { ret_code=x; fprintf(stderr, fmt, ##__VA_ARGS__); goto exit; }

int main(int argc, char *argv[]) {
    int ret_code = 0;
    FILE *file = NULL;
    unsigned char *uncompressed_data = NULL;
    unsigned char *compressed_data = NULL;
    size_t file_len;
    size_t compressed_len;

    TampCompressor compressor;
    TampConf compressor_conf = {.literal=8, .window=10, .use_custom_dictionary=false};
    unsigned char *compressor_buffer = NULL;

    if (!(compressor_buffer = malloc(1 << compressor_conf.window)))
        EXIT(1, "OOM");

    if (!(file = fopen("../../build/enwik8", "rb")))
        EXIT(1, "Unable to open file %s", "build/enwik8");

    fseek(file, 0, SEEK_END);
    file_len = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (!(uncompressed_data = malloc(file_len)))
        EXIT(1, "OOM");

    if (!(compressed_data = malloc(file_len)))
        EXIT(1, "OOM");

    fread(uncompressed_data, file_len, 1, file);

    if(TAMP_OK != tamp_compressor_init(&compressor, &compressor_conf, compressor_buffer))
        EXIT(1, "Failed to initialize compressor");

    printf("Beginning compressing...\n");
    if(TAMP_OK != tamp_compressor_compress(
                &compressor,
                compressed_data,
                file_len,
                &compressed_len,
                uncompressed_data,
                file_len,
                NULL
                ))
        EXIT(1, "Failed to compress data");

    printf("Compressed Length: %zu\n", compressed_len);

exit:
    if(uncompressed_data)
        free(uncompressed_data);
    if(compressed_data)
        free(compressed_data);
    if(compressor_buffer)
        free(compressor_buffer);
    if(file)
        fclose(file);

    return ret_code;
}
