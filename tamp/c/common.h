#ifndef TAMP_COMMON_H
#define TAMP_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TampConf {
    uint8_t window;  // number of window bits
    uint8_t literal;  // number of literal bits
} TampConf;

typedef struct TampRingBuffer {
    char *buffer;
    uint16_t pos;  // Current position
    uint16_t capacity;  // length of buffer
    uint16_t size;  // number of bytes populated
} RingBuffer;

#ifdef __cplusplus
}
#endif

#endif
