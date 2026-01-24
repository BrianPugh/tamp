/**
 * @file fatfs_ramdisk.h
 * @brief RAM disk interface for FatFs testing
 */
#ifndef FATFS_RAMDISK_H
#define FATFS_RAMDISK_H

#include <stddef.h>

/**
 * @brief Initialize RAM disk with the given buffer
 *
 * @param buffer Pre-allocated buffer for disk storage
 * @param size Size of buffer in bytes (should be multiple of 512)
 */
void fatfs_ramdisk_init(unsigned char *buffer, size_t size);

/**
 * @brief Get the size of the RAM disk in sectors
 */
size_t fatfs_ramdisk_sectors(void);

#endif /* FATFS_RAMDISK_H */
