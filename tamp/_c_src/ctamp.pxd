from libcpp cimport bool
from libc.stdint cimport uint8_t, uint32_t

cdef extern from "common.h":
    ctypedef struct TampConf:
        int window
        int literal
        bool use_custom_dictionary

    void initialize_dictionary(unsigned char *buffer, size_t size, uint32_t seed);
    int compute_min_pattern_size(uint8_t window, uint8_t literal);

cdef extern from "compressor.h":
    ctypedef struct TampCompressor:
        pass

    ctypedef enum tamp_res:
        TAMP_OK = 0
        TAMP_OUTPUT_FULL = 1
        TAMP_EXCESS_BITS = -1

    tamp_res tamp_compressor_init(TampCompressor *compressor, const TampConf *conf, unsigned char *window);

    void tamp_compressor_sink(
            TampCompressor *compressor,
            const unsigned char *input,
            size_t input_size,
            size_t *consumed_size
            );

    tamp_res tamp_compressor_compress_poll(
            TampCompressor *compressor,
            unsigned char *output,
            size_t output_size,
            size_t *output_written_size
            );

    tamp_res tamp_compressor_flush(
                    TampCompressor *compressor,
                    unsigned char *output,
                    size_t output_size,
                    size_t *output_written_size,
                    bool write_token
                    );

    tamp_res tamp_compressor_compress(
            TampCompressor *compressor,
            unsigned char *output,
            size_t output_size,
            size_t *output_written_size,
            const unsigned char *input,
            size_t input_size,
            size_t *input_consumed_size
            );
