/**
 * @file fatfs_ramdisk.c
 * @brief RAM disk implementation for FatFs testing
 *
 * Implements the diskio interface for a RAM-based disk.
 */
#include "fatfs_ramdisk.h"

#include <string.h>

// clang-format off
#include "fatfs/source/ff.h"     // Must come first - defines BYTE, LBA_t, etc.
#include "fatfs/source/diskio.h"
// clang-format on

#define SECTOR_SIZE 512

static unsigned char *ramdisk_buffer = NULL;
static size_t ramdisk_size = 0;

void fatfs_ramdisk_init(unsigned char *buffer, size_t size) {
    ramdisk_buffer = buffer;
    ramdisk_size = size;
    /* Initialize to 0xFF (erased state) */
    memset(buffer, 0xFF, size);
}

size_t fatfs_ramdisk_sectors(void) { return ramdisk_size / SECTOR_SIZE; }

/*-----------------------------------------------------------------------*/
/* FatFs diskio interface implementation                                 */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status(BYTE pdrv) {
    (void)pdrv;
    if (ramdisk_buffer == NULL) {
        return STA_NOINIT;
    }
    return 0;
}

DSTATUS disk_initialize(BYTE pdrv) {
    (void)pdrv;
    if (ramdisk_buffer == NULL) {
        return STA_NOINIT;
    }
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    (void)pdrv;
    if (ramdisk_buffer == NULL) {
        return RES_NOTRDY;
    }
    if ((sector + count) * SECTOR_SIZE > ramdisk_size) {
        return RES_PARERR;
    }
    memcpy(buff, ramdisk_buffer + sector * SECTOR_SIZE, count * SECTOR_SIZE);
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    (void)pdrv;
    if (ramdisk_buffer == NULL) {
        return RES_NOTRDY;
    }
    if ((sector + count) * SECTOR_SIZE > ramdisk_size) {
        return RES_PARERR;
    }
    memcpy(ramdisk_buffer + sector * SECTOR_SIZE, buff, count * SECTOR_SIZE);
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    (void)pdrv;
    if (ramdisk_buffer == NULL) {
        return RES_NOTRDY;
    }

    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;

        case GET_SECTOR_COUNT:
            *(LBA_t *)buff = ramdisk_size / SECTOR_SIZE;
            return RES_OK;

        case GET_SECTOR_SIZE:
            *(WORD *)buff = SECTOR_SIZE;
            return RES_OK;

        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1; /* Erase block size in sectors */
            return RES_OK;

        default:
            return RES_PARERR;
    }
}
