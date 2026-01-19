#ifndef TAMP_COMMON_H
#define TAMP_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#if ESP_PLATFORM
// (External) code #including this header MUST use the SAME TAMP_ESP32 setting that is used when
// building this lib!
// cppcheck-suppress missingInclude
#include "sdkconfig.h"
#endif

/* Should the ESP32-optimized variant be built? */
#ifdef CONFIG_TAMP_ESP32  // CONFIG_... from Kconfig takes precedence
#if CONFIG_TAMP_ESP32
#define TAMP_ESP32 1
#else
#define TAMP_ESP32 0
#endif
#endif

#ifndef TAMP_ESP32  // If not set via Kconfig, and not otherwise -D_efined, default TAMP_ESP32 to
                    // compatible version.
#define TAMP_ESP32 0
#endif

/* Compiler branch optimizations */
#if defined(__clang__) || (defined(__GNUC__) && (__GNUC__ > 2))
#define TAMP_LIKELY(c) (__builtin_expect(!!(c), 1))
#define TAMP_UNLIKELY(c) (__builtin_expect(!!(c), 0))
#else
#define TAMP_LIKELY(c) (c)
#define TAMP_UNLIKELY(c) (c)
#endif

#if defined(_MSC_VER)
#define TAMP_ALWAYS_INLINE __forceinline
#define TAMP_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define TAMP_ALWAYS_INLINE inline __attribute__((always_inline))
#define TAMP_NOINLINE __attribute__((noinline))
#else
#define TAMP_ALWAYS_INLINE inline
#define TAMP_NOINLINE
#endif

/* Include stream API (tamp_compress_stream, tamp_decompress_stream).
 * Enabled by default. Disable with -DTAMP_STREAM=0 to save ~2.8KB.
 */
#ifndef TAMP_STREAM
#define TAMP_STREAM 1
#endif

/* Work buffer size for stream API functions.
 * The buffer is allocated on the stack and split in half for input/output.
 * Larger values reduce I/O callback invocations, improving decompression speed.
 * Default of 32 bytes is safe for constrained stacks; 256+ bytes recommended
 * for better performance when stack space permits.
 * Override via compiler flag: -DTAMP_STREAM_WORK_BUFFER_SIZE=256
 */
#ifndef TAMP_STREAM_WORK_BUFFER_SIZE
#define TAMP_STREAM_WORK_BUFFER_SIZE 32
#endif

enum {
    /* Normal/Recoverable status >= 0 */
    TAMP_OK = 0,
    TAMP_OUTPUT_FULL = 1,      // Wasn't able to complete action due to full output buffer.
    TAMP_INPUT_EXHAUSTED = 2,  // Wasn't able to complete action due to exhausted input buffer.

    /* Error codes < 0 */
    TAMP_ERROR = -1,         // Generic error
    TAMP_EXCESS_BITS = -2,   // Provided symbol has more bits than conf->literal
    TAMP_INVALID_CONF = -3,  // Invalid configuration parameters.
    TAMP_OOB = -4,           // Out-of-bounds access detected in compressed data.
                             // Indicates malicious or corrupted input data attempting to
                             // reference memory outside the decompressor window buffer.

    /* Stream I/O error codes */
    TAMP_IO_ERROR = -10,     // Generic I/O error from read/write callback
    TAMP_READ_ERROR = -11,   // Read callback returned error
    TAMP_WRITE_ERROR = -12,  // Write callback returned error
};
typedef int8_t tamp_res;

typedef struct TampConf {
    uint16_t window : 4;                 // number of window bits
    uint16_t literal : 4;                // number of literal bits
    uint16_t use_custom_dictionary : 1;  // Use a custom initialized dictionary.
#if TAMP_LAZY_MATCHING
    uint16_t lazy_matching : 1;  // use Lazy Matching (spend 50-75% more CPU for around 0.5-2.0% better compression.)
                                 // only effects compression operations.
#endif
} TampConf;

/**
 * User-provied callback to be invoked after each compression cycle in the higher-level API.
 * @param[in,out] user_data Arbitrary user-provided data.
 * @param[in] bytes_processed Number of input bytes consumed so far.
 * @param[in] total_bytes Total number of input bytes.
 *
 * @return Some error code. If non-zero, abort current compression and return the value.
 *         For clarity, is is recommend to avoid already-used tamp_res values.
 *         e.g. start custom error codes at 100.
 */
typedef int (*tamp_callback_t)(void *user_data, size_t bytes_processed, size_t total_bytes);

/**
 * Stream read callback type for file/stream-based operations.
 *
 * Should behave like fread(): read up to `size` bytes into `buffer`.
 * Returns plain int (not tamp_res) for compatibility with standard I/O functions.
 * The stream API translates negative returns to TAMP_READ_ERROR.
 *
 * @param[in] handle User-provided handle (e.g., FILE*, lfs_file_t*, FIL*)
 * @param[out] buffer Buffer to read data into
 * @param[in] size Maximum number of bytes to read
 *
 * @return Number of bytes actually read (0 for EOF), or negative (e.g., -1) on error
 */
typedef int (*tamp_read_t)(void *handle, unsigned char *buffer, size_t size);

/**
 * Stream write callback type for file/stream-based operations.
 *
 * Should behave like fwrite(): write `size` bytes from `buffer`.
 * Returns plain int (not tamp_res) for compatibility with standard I/O functions.
 * The stream API treats negative returns or incomplete writes (fewer bytes than requested)
 * as TAMP_WRITE_ERROR. Chunks are small (at most TAMP_STREAM_WORK_BUFFER_SIZE/2 bytes),
 * so writing the full amount is expected.
 *
 * @param[in] handle User-provided handle (e.g., FILE*, lfs_file_t*, FIL*)
 * @param[in] buffer Buffer containing data to write
 * @param[in] size Number of bytes to write
 *
 * @return `size` on success, or negative (e.g., -1) on error
 */
typedef int (*tamp_write_t)(void *handle, const unsigned char *buffer, size_t size);

/*******************************************************************************
 * Built-in I/O handlers for common sources/sinks.
 *
 * Enable the ones you need by defining the appropriate macro in your build
 * system (e.g., -DTAMP_STREAM_STDIO=1):
 *
 *   - TAMP_STREAM_MEMORY  : Memory buffers (always available, no dependencies)
 *   - TAMP_STREAM_STDIO   : Standard C FILE* (POSIX, ESP-IDF VFS, etc.)
 *   - TAMP_STREAM_LITTLEFS: LittleFS filesystem
 *   - TAMP_STREAM_FATFS   : FatFs (ChaN's FAT filesystem)
 ******************************************************************************/

/* Memory buffer I/O */
#if TAMP_STREAM_MEMORY

/**
 * @brief Reader state for memory buffer input.
 *
 * Example:
 * @code
 * const unsigned char compressed_data[] = {...};
 * TampMemReader reader = {
 *     .data = compressed_data,
 *     .size = sizeof(compressed_data),
 *     .pos = 0
 * };
 * tamp_decompress_stream(tamp_stream_mem_read, &reader, ...);
 * @endcode
 */
typedef struct TampMemReader {
    const unsigned char *data; /**< Pointer to input data */
    size_t size;               /**< Total size of input data */
    size_t pos;                /**< Current read position (initialize to 0) */
} TampMemReader;

/**
 * @brief Writer state for memory buffer output.
 *
 * Example:
 * @code
 * unsigned char output[4096];
 * TampMemWriter writer = {
 *     .data = output,
 *     .capacity = sizeof(output),
 *     .pos = 0
 * };
 * tamp_compress_stream(..., tamp_stream_mem_write, &writer, ...);
 * // writer.pos contains bytes written
 * @endcode
 */
typedef struct TampMemWriter {
    unsigned char *data; /**< Pointer to output buffer */
    size_t capacity;     /**< Total capacity of output buffer */
    size_t pos;          /**< Current write position (initialize to 0) */
} TampMemWriter;

/**
 * @brief Read callback for memory buffers.
 * @param handle Pointer to TampMemReader.
 */
int tamp_stream_mem_read(void *handle, unsigned char *buffer, size_t size);

/**
 * @brief Write callback for memory buffers.
 * @param handle Pointer to TampMemWriter.
 * @return Bytes written, or -1 if buffer would overflow.
 */
int tamp_stream_mem_write(void *handle, const unsigned char *buffer, size_t size);

#endif /* TAMP_STREAM_MEMORY */

/* POSIX / Standard C stdio (FILE*) */
#if TAMP_STREAM_STDIO

/**
 * @brief Read callback for stdio FILE*.
 * @param handle FILE* opened for reading.
 */
int tamp_stream_stdio_read(void *handle, unsigned char *buffer, size_t size);

/**
 * @brief Write callback for stdio FILE*.
 * @param handle FILE* opened for writing.
 */
int tamp_stream_stdio_write(void *handle, const unsigned char *buffer, size_t size);

#endif /* TAMP_STREAM_STDIO */

/* LittleFS */
#if TAMP_STREAM_LITTLEFS

#include "lfs.h"

/**
 * @brief Bundle struct for LittleFS file operations.
 *
 * LittleFS API requires both the filesystem context and file handle.
 */
typedef struct TampLfsFile {
    lfs_t *lfs;       /**< Pointer to mounted LittleFS instance */
    lfs_file_t *file; /**< Pointer to opened file handle */
} TampLfsFile;

/**
 * @brief Read callback for LittleFS.
 * @param handle Pointer to TampLfsFile.
 */
int tamp_stream_lfs_read(void *handle, unsigned char *buffer, size_t size);

/**
 * @brief Write callback for LittleFS.
 * @param handle Pointer to TampLfsFile.
 */
int tamp_stream_lfs_write(void *handle, const unsigned char *buffer, size_t size);

#endif /* TAMP_STREAM_LITTLEFS */

/* FatFs (ChaN's FAT Filesystem) */
#if TAMP_STREAM_FATFS

#include "ff.h"

/**
 * @brief Read callback for FatFs.
 * @param handle Pointer to FIL (FatFs file object).
 */
int tamp_stream_fatfs_read(void *handle, unsigned char *buffer, size_t size);

/**
 * @brief Write callback for FatFs.
 * @param handle Pointer to FIL (FatFs file object).
 */
int tamp_stream_fatfs_write(void *handle, const unsigned char *buffer, size_t size);

#endif /* TAMP_STREAM_FATFS */

/**
 * @brief Pre-populate a window buffer with common characters.
 *
 * @param[out] buffer Populated output buffer.
 * @param[in] size Size of output buffer in bytes.
 */
void tamp_initialize_dictionary(unsigned char *buffer, size_t size);

/**
 * @brief Compute the minimum viable pattern size given window and literal config parameters.
 *
 * @param[in] window Number of window bits. Valid values are [8, 15].
 * @param[in] literal Number of literal bits. Valid values are [5, 8].
 *
 * @return The minimum pattern size in bytes. Either 2 or 3.
 */
int8_t tamp_compute_min_pattern_size(uint8_t window, uint8_t literal);

#ifdef __cplusplus
}
#endif

#endif
