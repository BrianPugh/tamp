#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

static const unsigned char common_characters[] = {
    0x20, 0x00, 0x30, 0x65, 0x69, 0x3e, 0x74, 0x6f,
    0x3c, 0x61, 0x6e, 0x73, 0xa, 0x72, 0x2f, 0x2e
};


static inline uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

void initialize_dictionary(unsigned char *buffer, size_t size, uint32_t seed){
    if (seed == 0) {
        memset(buffer, 0, size);
        return;
    }
    for(size_t i=0; i < size; i+=8){
        xorshift32(&seed);
        buffer[i + 0] = common_characters[seed & 0x0F];
        buffer[i + 1] = common_characters[seed >> 4 & 0x0F];
        buffer[i + 2] = common_characters[seed >> 8 & 0x0F];
        buffer[i + 3] = common_characters[seed >> 12 & 0x0F];
        buffer[i + 4] = common_characters[seed >> 16 & 0x0F];
        buffer[i + 5] = common_characters[seed >> 20 & 0x0F];
        buffer[i + 6] = common_characters[seed >> 24 & 0x0F];
        buffer[i + 7] = common_characters[seed >> 28 & 0x0F];
    }
}


/**
 * @brief Compute whether the minimum pattern length should be 2 or 3.
 */
int8_t compute_min_pattern_size(uint8_t window, uint8_t literal) {
    switch(literal){
        case 5:
            return 2 + (window > 10);
        case 6:
            return 2 + (window > 12);
        case 7:
            return 2 + (window > 14);
        case 8:
            return 2;
        default:
            return -1;
    }
}
