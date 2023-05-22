from libcpp cimport bool
from libc.stdint cimport uint8_t, uint32_t

cdef extern from "tamp/common.h":
    ctypedef struct TampConf:
        int window
        int literal
        bool use_custom_dictionary

    ctypedef enum tamp_res:
        # Normal/Recoverable status >= 0
        TAMP_OK = 0,
        TAMP_OUTPUT_FULL = 1,  # Wasn't able to complete action due to full output buffer.
        TAMP_INPUT_EXHAUSTED = 2, # Wasn't able to complete action due to exhausted input buffer.

        # Error Codes < 0
        TAMP_ERROR = -1,  # Generic error
        TAMP_EXCESS_BITS = -2,  # Provided symbol has more bits than conf->literal
        TAMP_INVALID_CONF = -3,  # Invalid configuration parameters.

    void initialize_dictionary(unsigned char *buffer, size_t size, uint32_t seed);
    int compute_min_pattern_size(uint8_t window, uint8_t literal);


cdef extern from "tamp/compressor.h":
    ctypedef struct TampCompressor:
        pass

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


cdef extern from "tamp/decompressor.h":
    ctypedef struct TampDecompressor:
        pass

    tamp_res tamp_decompressor_read_header(
            TampConf *conf,
            const unsigned char *input,
            size_t input_size,
            size_t *input_consumed_size
    );

    tamp_res tamp_decompressor_init(
            TampDecompressor *decompressor,
            const TampConf *conf,
            unsigned char *window
    );

    tamp_res tamp_decompressor_decompress(
            TampDecompressor *decompressor,
            unsigned char *output,
            size_t output_size,
            size_t *output_written_size,
            const unsigned char *input,
            size_t input_size,
            size_t *input_consumed_size
    );
