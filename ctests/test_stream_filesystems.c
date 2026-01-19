/**
 * @file test_stream_filesystems.c
 * @brief End-to-end tests for LittleFS and FatFs stream handlers
 *
 * These tests verify that the tamp stream API works correctly with
 * real filesystem implementations using RAM-backed storage.
 */
#include <string.h>

#include "tamp/compressor.h"
#include "tamp/decompressor.h"
#include "unity.h"

#ifdef TEST_LITTLEFS
#include "lfs_rambd.h"
#include "littlefs/lfs.h"

/* LittleFS test storage - 32KB RAM filesystem */
#define LFS_STORAGE_SIZE (32 * 1024)
static unsigned char lfs_storage[LFS_STORAGE_SIZE];

void test_littlefs_roundtrip(void) {
    /* Initialize RAM block device */
    lfs_rambd_t rambd;
    struct lfs_config cfg;
    lfs_rambd_create(&cfg, &rambd, lfs_storage, LFS_STORAGE_SIZE);

    /* Format and mount filesystem */
    lfs_t lfs;
    int err = lfs_format(&lfs, &cfg);
    TEST_ASSERT_EQUAL(0, err);

    err = lfs_mount(&lfs, &cfg);
    TEST_ASSERT_EQUAL(0, err);

    /* Test data */
    const char *original =
        "LittleFS compression test! "
        "This data will be compressed and decompressed. "
        "LittleFS compression test! LittleFS compression test!";
    size_t original_len = strlen(original);

    /* Write original data to input file */
    lfs_file_t input_file;
    err = lfs_file_open(&lfs, &input_file, "input.txt", LFS_O_WRONLY | LFS_O_CREAT);
    TEST_ASSERT_EQUAL(0, err);
    lfs_ssize_t written = lfs_file_write(&lfs, &input_file, original, original_len);
    TEST_ASSERT_EQUAL(original_len, written);
    lfs_file_close(&lfs, &input_file);

    /* Open files for compression */
    lfs_file_t in_file, compressed_file;
    err = lfs_file_open(&lfs, &in_file, "input.txt", LFS_O_RDONLY);
    TEST_ASSERT_EQUAL(0, err);
    err = lfs_file_open(&lfs, &compressed_file, "compressed.tamp", LFS_O_WRONLY | LFS_O_CREAT);
    TEST_ASSERT_EQUAL(0, err);

    /* Compress using LittleFS handlers */
    unsigned char window[1 << 10];
    TampLfsFile tamp_in = {.lfs = &lfs, .file = &in_file};
    TampLfsFile tamp_out = {.lfs = &lfs, .file = &compressed_file};

    /* Initialize compressor */
    TampCompressor compressor;
    tamp_res res = tamp_compressor_init(&compressor, NULL, window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    size_t compress_in, compress_out;
    res = tamp_compress_stream(&compressor, tamp_stream_lfs_read, &tamp_in, tamp_stream_lfs_write, &tamp_out,
                               &compress_in, &compress_out, NULL, NULL);

    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(original_len, compress_in);
    TEST_ASSERT_GREATER_THAN(0, compress_out);

    lfs_file_close(&lfs, &in_file);
    lfs_file_close(&lfs, &compressed_file);

    /* Open files for decompression */
    lfs_file_t decomp_in, decomp_out;
    err = lfs_file_open(&lfs, &decomp_in, "compressed.tamp", LFS_O_RDONLY);
    TEST_ASSERT_EQUAL(0, err);
    err = lfs_file_open(&lfs, &decomp_out, "output.txt", LFS_O_WRONLY | LFS_O_CREAT);
    TEST_ASSERT_EQUAL(0, err);

    /* Decompress */
    TampLfsFile tamp_decomp_in = {.lfs = &lfs, .file = &decomp_in};
    TampLfsFile tamp_decomp_out = {.lfs = &lfs, .file = &decomp_out};

    /* Initialize decompressor */
    TampDecompressor decompressor;
    res = tamp_decompressor_init(&decompressor, NULL, window, 10);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    size_t decompress_in, decompress_out;
    res = tamp_decompress_stream(&decompressor, tamp_stream_lfs_read, &tamp_decomp_in, tamp_stream_lfs_write,
                                 &tamp_decomp_out, &decompress_in, &decompress_out, NULL, NULL);

    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(compress_out, decompress_in);
    TEST_ASSERT_EQUAL(original_len, decompress_out);

    lfs_file_close(&lfs, &decomp_in);
    lfs_file_close(&lfs, &decomp_out);

    /* Verify decompressed data */
    lfs_file_t verify_file;
    err = lfs_file_open(&lfs, &verify_file, "output.txt", LFS_O_RDONLY);
    TEST_ASSERT_EQUAL(0, err);

    char decompressed[256];
    lfs_ssize_t read_back = lfs_file_read(&lfs, &verify_file, decompressed, sizeof(decompressed));
    TEST_ASSERT_EQUAL(original_len, read_back);
    TEST_ASSERT_EQUAL_MEMORY(original, decompressed, original_len);

    lfs_file_close(&lfs, &verify_file);

    /* Cleanup */
    lfs_unmount(&lfs);
}

#endif /* TEST_LITTLEFS */

#ifdef TEST_FATFS
#include "fatfs/source/ff.h"
#include "fatfs_ramdisk.h"

/* FatFs test storage - 256KB RAM disk for valid FAT filesystem */
#define FATFS_STORAGE_SIZE (256 * 1024)
static unsigned char fatfs_storage[FATFS_STORAGE_SIZE];

void test_fatfs_roundtrip(void) {
    /* Initialize RAM disk */
    fatfs_ramdisk_init(fatfs_storage, FATFS_STORAGE_SIZE);

    /* Mount filesystem (will auto-format on first use) */
    FATFS fs;
    FRESULT fres;

    /* Register the filesystem object first (mount = 0 means don't mount yet) */
    fres = f_mount(&fs, "0:", 0);
    TEST_ASSERT_EQUAL(FR_OK, fres);

    /* Format the RAM disk */
    BYTE work_buf[4096]; /* f_mkfs needs larger work buffer */
    MKFS_PARM opt = {
        .fmt = FM_FAT, /* FAT12/16 */
        .n_fat = 1,    /* Number of FATs */
        .align = 0,    /* Auto alignment */
        .n_root = 0,   /* Auto root entries */
        .au_size = 0   /* Auto cluster size */
    };
    fres = f_mkfs("0:", &opt, work_buf, sizeof(work_buf));
    TEST_ASSERT_EQUAL(FR_OK, fres);

    /* Remount with force flag to actually mount the filesystem */
    fres = f_mount(&fs, "0:", 1);
    TEST_ASSERT_EQUAL(FR_OK, fres);

    /* Test data */
    const char *original =
        "FatFs compression test! "
        "This data will be compressed and decompressed using FatFs. "
        "FatFs compression test! FatFs compression test!";
    size_t original_len = strlen(original);

    /* Write original data to input file */
    FIL input_file;
    fres = f_open(&input_file, "0:input.txt", FA_WRITE | FA_CREATE_ALWAYS);
    TEST_ASSERT_EQUAL(FR_OK, fres);
    UINT bw;
    fres = f_write(&input_file, original, original_len, &bw);
    TEST_ASSERT_EQUAL(FR_OK, fres);
    TEST_ASSERT_EQUAL(original_len, bw);
    f_close(&input_file);

    /* Open files for compression */
    FIL in_file, compressed_file;
    fres = f_open(&in_file, "0:input.txt", FA_READ);
    TEST_ASSERT_EQUAL(FR_OK, fres);
    fres = f_open(&compressed_file, "0:comp.tpm", FA_WRITE | FA_CREATE_ALWAYS);
    TEST_ASSERT_EQUAL(FR_OK, fres);

    /* Compress using FatFs handlers */
    unsigned char window[1 << 10];

    /* Initialize compressor */
    TampCompressor compressor;
    tamp_res res = tamp_compressor_init(&compressor, NULL, window);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    size_t compress_in, compress_out;
    res = tamp_compress_stream(&compressor, tamp_stream_fatfs_read, &in_file, tamp_stream_fatfs_write, &compressed_file,
                               &compress_in, &compress_out, NULL, NULL);

    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(original_len, compress_in);
    TEST_ASSERT_GREATER_THAN(0, compress_out);

    f_close(&in_file);
    f_close(&compressed_file);

    /* Open files for decompression */
    FIL decomp_in, decomp_out;
    fres = f_open(&decomp_in, "0:comp.tpm", FA_READ);
    TEST_ASSERT_EQUAL(FR_OK, fres);
    fres = f_open(&decomp_out, "0:output.txt", FA_WRITE | FA_CREATE_ALWAYS);
    TEST_ASSERT_EQUAL(FR_OK, fres);

    /* Initialize decompressor */
    TampDecompressor decompressor;
    res = tamp_decompressor_init(&decompressor, NULL, window, 10);
    TEST_ASSERT_EQUAL(TAMP_OK, res);

    /* Decompress */
    size_t decompress_in, decompress_out;
    res = tamp_decompress_stream(&decompressor, tamp_stream_fatfs_read, &decomp_in, tamp_stream_fatfs_write,
                                 &decomp_out, &decompress_in, &decompress_out, NULL, NULL);

    TEST_ASSERT_EQUAL(TAMP_OK, res);
    TEST_ASSERT_EQUAL(compress_out, decompress_in);
    TEST_ASSERT_EQUAL(original_len, decompress_out);

    f_close(&decomp_in);
    f_close(&decomp_out);

    /* Verify decompressed data */
    FIL verify_file;
    fres = f_open(&verify_file, "0:output.txt", FA_READ);
    TEST_ASSERT_EQUAL(FR_OK, fres);

    char decompressed[256];
    UINT br;
    fres = f_read(&verify_file, decompressed, sizeof(decompressed), &br);
    TEST_ASSERT_EQUAL(FR_OK, fres);
    TEST_ASSERT_EQUAL(original_len, br);
    TEST_ASSERT_EQUAL_MEMORY(original, decompressed, original_len);

    f_close(&verify_file);

    /* Cleanup */
    f_unmount("0:");
}

#endif /* TEST_FATFS */
