#ifndef TAMP_COMMON_H
#define TAMP_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

typedef struct TampConf {
    uint16_t window:4;  // number of window bits
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
void initialize_dictionary(char *buffer, size_t size, uint32_t seed);

/**
 * @brief
 *
 * @param[in] window Number of window bits.
 * @param[in] literal Number of literal bits.
 *
 * @return The minimum pattern size in bytes.
 */
int8_t compute_min_pattern_size(uint8_t window, uint8_t literal);

#ifdef __cplusplus
}
#endif

#endif
