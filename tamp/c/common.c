#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

static const char common_characters[] = {
    0x20, 0x0, 0x30, 0x65, 0x69, 0x3e, 0x74, 0x6f,
    0x3c, 0x61, 0x6e, 0x73, 0xa, 0x72, 0x2f, 0x2e
};


inline uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

void initialize_dictionary(char *buffer, uint16_t size, uint32_t seed){
    for(uint16_t i; i < (size >> 3); i+=8){
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

/***************
 * Ring Buffer *
 ***************/

void ring_buffer_init(RingBuffer *rb, char *buffer) {
    rb->buffer = buffer;
    rb->size = 0;

}

#ifdef TAMP_DYNAMIC_ALLOCATION
RingBuffer *ring_buffer_create(size_t size) {
    RingBuffer *rb = (RingBuffer *)malloc(sizeof(RingBuffer));
    if (!rb) {
        return NULL;
    }

    rb->dynamic_buffer = (char *)malloc(size);
    if (!rb->dynamic_buffer) {
        free(rb);
        return NULL;
    }

    rb->buffer = rb->dynamic_buffer;
    rb->size = size;
    rb->head = rb->buffer;
    rb->tail = rb->buffer;

    return rb;
}

void ring_buffer_destroy(RingBuffer *rb) {
    if (rb) {
        free(rb->dynamic_buffer);
        free(rb);
    }
}
#else  // not defined TAMP_DYNAMIC_ALLOCATION
RingBuffer *ring_buffer_create(size_t size, char *buffer) {
    RingBuffer *rb = (RingBuffer *)malloc(sizeof(RingBuffer));
    if (!rb) {
        return NULL;
    }

    rb->buffer = buffer;
    rb->size = size;
    rb->head = rb->buffer;
    rb->tail = rb->buffer;

    return rb;
}

void ring_buffer_destroy(RingBuffer *rb) {
    if (rb) {
        free(rb);
    }
}
#endif // TAMP_DYNAMIC_ALLOCATION

int ring_buffer_write(RingBuffer *rb, const char *data, size_t data_size) {
    if (!rb || !data || data_size > rb->size) {
        return -1;
    }

    for (size_t i = 0; i < data_size; i++) {
        *rb->head = data[i];
        rb->head++;

        if (rb->head >= rb->buffer + rb->size) {
            rb->head = rb->buffer;
        }
    }

    return 0;
}

int ring_buffer_read(RingBuffer *rb, char *data, size_t data_size) {
    if (!rb || !data || data_size > rb->size) {
        return -1;
    }

    for (size_t i = 0; i < data_size; i++) {
        data[i] = *rb->tail;
        rb->tail++;

        if (rb->tail >= rb->buffer + rb->size) {
            rb->tail = rb->buffer;
        }
    }

    return 0;
}

int main() {
#ifdef TAMP_DYNAMIC_ALLOCATION
    RingBuffer *rb = ring_buffer_create(10);
#else
    char buffer[10];
    RingBuffer *rb = ring_buffer_create(10, buffer);
#endif
    if (!rb) {
        printf("Failed to create ring buffer.\n");
        return 1;
    }

    const char *data = "HelloWorld";
    ring_buffer_write(rb, data, strlen(data));

    char read_data[11];
    memset(read_data, 0, sizeof(read_data));
    ring_buffer_read(rb, read_data, 10);
    printf("Read data: %s\n", read_data);

    ring_buffer_destroy(rb);
    return 0;
}
