/**
 * @file lfs_rambd.h
 * @brief RAM block device for LittleFS testing
 */
#ifndef LFS_RAMBD_H
#define LFS_RAMBD_H

#include "littlefs/lfs.h"

typedef struct {
    unsigned char *buffer;
    lfs_size_t read_size;
    lfs_size_t prog_size;
    lfs_size_t block_size;
    lfs_size_t block_count;
} lfs_rambd_t;

/**
 * @brief Initialize RAM block device configuration
 *
 * @param cfg LittleFS config to populate
 * @param bd RAM block device context
 * @param buffer Pre-allocated buffer for filesystem storage
 * @param size Size of buffer in bytes
 */
void lfs_rambd_create(struct lfs_config *cfg, lfs_rambd_t *bd, unsigned char *buffer, lfs_size_t size);

#endif /* LFS_RAMBD_H */
