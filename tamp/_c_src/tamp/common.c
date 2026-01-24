#include "common.h"

#if TAMP_STREAM && TAMP_STREAM_WORK_BUFFER_SIZE < 4
#error "TAMP_STREAM_WORK_BUFFER_SIZE must be at least 4 bytes"
#endif

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if TAMP_STREAM_STDIO
#include <stdio.h>
#endif

static const unsigned char common_characters[] = {0x20, 0x00, 0x30, 0x65, 0x69, 0x3e, 0x74, 0x6f,
                                                  0x3c, 0x61, 0x6e, 0x73, 0xa,  0x72, 0x2f, 0x2e};

static inline uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

void tamp_initialize_dictionary(unsigned char *buffer, size_t size) {
    uint32_t seed = 3758097560;  // This was experimentally discovered with tools/find_seed.py
    uint32_t randbuf = 0;
    for (size_t i = 0; i < size; i++) {
        if (TAMP_UNLIKELY((i & 0x7) == 0)) randbuf = xorshift32(&seed);
        buffer[i] = common_characters[randbuf & 0x0F];
        randbuf >>= 4;
    }
}

int8_t tamp_compute_min_pattern_size(uint8_t window, uint8_t literal) {
    return 2 + (window > (10 + ((literal - 5) << 1)));
}

/*******************************************************************************
 * Built-in I/O handler implementations
 ******************************************************************************/

#if TAMP_STREAM_MEMORY

int tamp_stream_mem_read(void *handle, unsigned char *buffer, size_t size) {
    TampMemReader *r = (TampMemReader *)handle;
    size_t available = r->size - r->pos;
    size_t to_read = (size < available) ? size : available;
    if (to_read > (size_t)INT_MAX) to_read = (size_t)INT_MAX;
    memcpy(buffer, r->data + r->pos, to_read);
    r->pos += to_read;
    return (int)to_read;
}

int tamp_stream_mem_write(void *handle, const unsigned char *buffer, size_t size) {
    TampMemWriter *w = (TampMemWriter *)handle;
    size_t available = w->capacity - w->pos;
    if (size > available) return -1;
    if (size > (size_t)INT_MAX) return -1;
    memcpy(w->data + w->pos, buffer, size);
    w->pos += size;
    return (int)size;
}

#endif /* TAMP_STREAM_MEMORY */

#if TAMP_STREAM_STDIO

int tamp_stream_stdio_read(void *handle, unsigned char *buffer, size_t size) {
    FILE *f = (FILE *)handle;
    size_t bytes_read = fread(buffer, 1, size, f);
    if (bytes_read == 0 && ferror(f)) return -1;
    return (int)bytes_read;
}

int tamp_stream_stdio_write(void *handle, const unsigned char *buffer, size_t size) {
    FILE *f = (FILE *)handle;
    size_t bytes_written = fwrite(buffer, 1, size, f);
    if (bytes_written < size && ferror(f)) return -1;
    return (int)bytes_written;
}

#endif /* TAMP_STREAM_STDIO */

#if TAMP_STREAM_LITTLEFS

int tamp_stream_lfs_read(void *handle, unsigned char *buffer, size_t size) {
    TampLfsFile *f = (TampLfsFile *)handle;
    lfs_ssize_t result = lfs_file_read(f->lfs, f->file, buffer, size);
    return (int)result;
}

int tamp_stream_lfs_write(void *handle, const unsigned char *buffer, size_t size) {
    TampLfsFile *f = (TampLfsFile *)handle;
    lfs_ssize_t result = lfs_file_write(f->lfs, f->file, buffer, size);
    return (int)result;
}

#endif /* TAMP_STREAM_LITTLEFS */

#if TAMP_STREAM_FATFS

int tamp_stream_fatfs_read(void *handle, unsigned char *buffer, size_t size) {
    UINT bytes_read;
    FRESULT res = f_read((FIL *)handle, buffer, (UINT)size, &bytes_read);
    if (res != FR_OK) return -1;
    return (int)bytes_read;
}

int tamp_stream_fatfs_write(void *handle, const unsigned char *buffer, size_t size) {
    UINT bytes_written;
    FRESULT res = f_write((FIL *)handle, buffer, (UINT)size, &bytes_written);
    if (res != FR_OK) return -1;
    return (int)bytes_written;
}

#endif /* TAMP_STREAM_FATFS */
