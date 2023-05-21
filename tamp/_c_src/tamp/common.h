#ifndef TAMP_COMMON_H
#define TAMP_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    /* Normal/Recoverable status >= 0 */
    TAMP_OK = 0,
    TAMP_OUTPUT_FULL = 1,  // Wasn't able to complete action due to full output buffer.

    /* Error codes < 0 */
    TAMP_EXCESS_BITS = -1,  // Provided symbol has more bits than conf->literal
    TAMP_INVALID_CONF = -2,  // Invalid configuration parameters.
    TAMP_INPUT_EXHAUSTED = -3, // Wasn't able to complete action due to exhausted input buffer.
    TAMP_INVALID_SYMBOL = -4,  // Unknown huffman code encountered.
} tamp_res;

typedef struct TampConf {
    uint16_t window:4;   // number of window bits
    uint16_t literal:4;  // number of literal bits
    uint16_t use_custom_dictionary:1;  // Use a custom initialized dictionary.
} TampConf;

/**
 * @brief Pre-populate a window buffer with common characters.
 *
 * @param[out] buffer Populated output buffer.
 * @param[in] size Size of output buffer.
 * @param[in] seed Pseudorandom generator initial seed.
 */
void tamp_initialize_dictionary(unsigned char *buffer, size_t size);

/**
 * @brief Pre-populate a window buffer with common characters with a non-standard seed.
 *
 * @param[out] buffer Populated output buffer.
 * @param[in] size Size of output buffer.
 * @param[in] seed Pseudorandom generator initial seed.
 */
void tamp_initialize_dictionary_seed(unsigned char *buffer, size_t size, uint32_t seed);


/**
 * @brief Compute the minimum viable pattern size given window and literal config parameters.
 *
 * @param[in] window Number of window bits.
 * @param[in] literal Number of literal bits.
 *
 * @return The minimum pattern size in bytes.
 */
int8_t tamp_compute_min_pattern_size(uint8_t window, uint8_t literal);

#ifdef __cplusplus
}
#endif

#endif
