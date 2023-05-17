#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "tamp/compressor.h"

#define EXIT(x, fmt, ...) { ret_code=x; fprintf(stderr, fmt, ##__VA_ARGS__); goto exit; }

int main(int argc, char *argv[]) {
    int ret_code = 0;
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
                &consumed
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

    return ret_code;
}
