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

/* Per-function optimize attributes and #pragma GCC push/pop_options require
 * GCC on a target that supports them. Xtensa GCC does not. */
#if defined(__GNUC__) && !defined(__clang__) && !defined(__XTENSA__)
#define TAMP_HAS_GCC_OPTIMIZE 1
#else
#define TAMP_HAS_GCC_OPTIMIZE 0
#endif

#if defined(_MSC_VER)
#define TAMP_ALWAYS_INLINE __forceinline
#define TAMP_NOINLINE __declspec(noinline)
#define TAMP_OPTIMIZE_SIZE /* not supported */
#elif defined(__GNUC__) && !defined(__clang__)
#define TAMP_ALWAYS_INLINE inline __attribute__((always_inline))
#define TAMP_NOINLINE __attribute__((noinline))
#if TAMP_HAS_GCC_OPTIMIZE
#define TAMP_OPTIMIZE_SIZE __attribute__((optimize("Os")))
#else
#define TAMP_OPTIMIZE_SIZE
#endif
#elif defined(__clang__)
#define TAMP_ALWAYS_INLINE inline __attribute__((always_inline))
#define TAMP_NOINLINE __attribute__((noinline))
#define TAMP_OPTIMIZE_SIZE /* clang doesn't support per-function optimize */
#else
#define TAMP_ALWAYS_INLINE inline
#define TAMP_NOINLINE
#define TAMP_OPTIMIZE_SIZE
#endif

/*******************************************************************************
 * Platform performance tuning
 *
 * The core sources never select architecture-specific code on their own:
 * every flag below defaults to the portable implementation. Build systems opt
 * in to the measured configuration for their platform, mirroring TAMP_ESP32:
 *
 *   pip/Cython (setup.py):      TAMP_USE_DESKTOP_MATCH=1 on 64-bit hosts
 *   espidf component (Kconfig): TAMP_ESP32 (default y)
 *   ARMv7E-M (Cortex-M4/M7):    TAMP_ARMV7EM=1 (profile flag, see below)
 *
 * Individual flags can still be set/overridden with -D<flag>=0/1. Measured
 * numbers below are from devices/BENCHMARKS.md workloads; when enabling a
 * flag on an unmeasured core, benchmark it.
 ******************************************************************************/

/* Profile for ARMv7E-M cores (Cortex-M4/M7): little-endian with cheap
 * unaligned loads. Enables the prefilter match finder and every decompressor
 * fast path below (measured on STM32H7B0/Cortex-M7 vs the portable build:
 * 1.31x compression, 1.92x decompression, ~5.2 KB additional flash). */
#ifndef TAMP_ARMV7EM
#define TAMP_ARMV7EM 0
#endif

/* find_best_match implementation (see compressor.c). At most one of these
 * may be 1 (enforced below); with none set, the portable scan is used.
 *   embedded:  portable single-byte-first scan, the default.
 *   swar32:    experimental 32-bit SWAR (candidate for single-issue cores;
 *              measured 0.93x on Cortex-M7 - opt-in only).
 *   desktop:   64-bit SWAR for 64-bit hosts (little-endian, cheap unaligned
 *              loads; measured ~2x the prefilter there).
 *   prefilter: first-byte prefilter (1.36x compression on Cortex-M7;
 *              ~0.5x on out-of-order 64-bit hosts). */
#ifndef TAMP_USE_EMBEDDED_MATCH
#define TAMP_USE_EMBEDDED_MATCH 0
#endif
#ifndef TAMP_USE_SWAR32_MATCH
#define TAMP_USE_SWAR32_MATCH 0
#endif
#ifndef TAMP_USE_DESKTOP_MATCH
#define TAMP_USE_DESKTOP_MATCH 0
#endif
#ifndef TAMP_USE_PREFILTER_MATCH
#define TAMP_USE_PREFILTER_MATCH \
    (TAMP_ARMV7EM && !TAMP_USE_EMBEDDED_MATCH && !TAMP_USE_SWAR32_MATCH && !TAMP_USE_DESKTOP_MATCH)
#endif

/* The selections are mutually exclusive; reject conflicting configurations
 * loudly rather than silently picking one. TAMP_ESP32 counts as a selection:
 * the espidf platform component provides find_best_match via extern, and
 * compressor.c's dispatch checks it first, so combining it with an explicit
 * TAMP_USE_*_MATCH would otherwise silently drop the requested finder. */
#if (TAMP_USE_EMBEDDED_MATCH + TAMP_USE_SWAR32_MATCH + TAMP_USE_DESKTOP_MATCH + TAMP_USE_PREFILTER_MATCH + \
     TAMP_ESP32) > 1
#error "At most one find_best_match selection (TAMP_USE_*_MATCH / TAMP_ESP32) may be enabled"
#endif

/* tamp_window_copy variant with a no-wrap fast path (see common.c).
 * Measured: +14% decompression on Cortex-M7; -3% on Xtensa LX7; ~+160 bytes
 * on Cortex-M0/M0+ where flash is the scarce resource. */
#ifndef TAMP_FAST_WINDOW_COPY
#define TAMP_FAST_WINDOW_COPY TAMP_ARMV7EM
#endif

/* refill_bit_buffer on locals with a single writeback (see decompressor.c).
 * Measured: +5% decompression on Cortex-M7; -3% on Xtensa LX7; +324 bytes on
 * Cortex-M0/M0+ (register spills on the 8-register file). */
#ifndef TAMP_FAST_BIT_REFILL
#define TAMP_FAST_BIT_REFILL TAMP_ARMV7EM
#endif

/* Word-at-a-time TAMP_COPY_TO_OUTPUT (see decompressor.c). Requires cheap
 * unaligned 32-bit access. */
#ifndef TAMP_FAST_OUTPUT_COPY
#define TAMP_FAST_OUTPUT_COPY TAMP_ARMV7EM
#endif

/* Source the window update from the just-written output snapshot instead of
 * calling tamp_window_copy (see decompressor.c). Removes the call plus its
 * reverse-copy overlap logic from the hot path. Measured: -4% (M7) / -6.7%
 * (M4) core insns/byte, but +3% on Cortex-M0+ (inlining the update costs more
 * than the call on the 8-register file), so it defaults off there. */
#ifndef TAMP_WINDOW_FROM_OUTPUT
#define TAMP_WINDOW_FROM_OUTPUT TAMP_ARMV7EM
#endif

/* Checked-once fast inner decode loop (see decompressor.c). Under a
 * per-iteration precondition (>=4 input bytes, >=32 output bytes, no pending
 * skip/extended/flush state, no callback) every mid-token bounds/exhaustion
 * check on the classic token path is provably dead and removed. The loop keeps
 * the undecoded bits in a 64-bit reservoir local and refills 4 whole bytes at
 * a time only when <=32 bits remain (~every 1-2 tokens); on loop exit the
 * reservoir is written back to the 32-bit struct bit_buffer, surplus whole
 * bytes are pushed back to the input cursor, and a checked refill restores the
 * >=25-bit invariant the careful body relies on. The reservoir alone (measured
 * against an earlier fast-loop variant that refilled the 32-bit struct
 * bit_buffer per token, since removed) was worth +14.2% decompression
 * throughput on STM32H7B0/M7 hardware, +4.7% AND slightly smaller code on
 * RP2040/M0+ hardware despite the __aeabi_ll* helper calls for the 64-bit
 * shifts, and -12.0% core insns/byte on QEMU m33 (hardware-unverified) on the
 * window=10 enwik8 workload. Duplicates the classic token path (a few hundred
 * bytes of flash), so it defaults off on the portable/M0+ build and on where
 * the platform profile already opts into the other fast paths. */
#ifndef TAMP_FAST_DECODE_LOOP
#define TAMP_FAST_DECODE_LOOP TAMP_ARMV7EM
#endif

/* Compile the careful body of tamp_decompressor_decompress_cb -Os (see
 * decompressor.c; GCC-only, no-op elsewhere). Only sensible with
 * TAMP_FAST_DECODE_LOOP, which routes every hot token through the extracted
 * -O3 fast-loop function and leaves the careful body handling buffer tails,
 * resume state, and extended dispatch glue. Measured on the window=10 enwik8
 * workload: -872 B of decompressor .text on Cortex-M7 for no throughput
 * change; +64 B on Cortex-M0+ (the 8-register file spills more under Os), so
 * it stays off outside the ARMV7EM profile. */
#ifndef TAMP_COMPACT_CAREFUL_BODY
#define TAMP_COMPACT_CAREFUL_BODY TAMP_ARMV7EM
#endif

/* Compile-time-pinned stream configuration (opt-in; decompressor only).
 * Most embedded deployments only ever decode streams produced with one fixed
 * configuration. Defining these pins the window and/or literal bit counts at
 * compile time: the decompressor folds every window_mask / window_size / bit
 * shift to an immediate, and with BOTH pinned min_pattern_size becomes a
 * compile-time constant too. A pinned build REJECTS (TAMP_INVALID_CONF) any
 * stream whose header disagrees, so this is only safe when every stream really
 * uses the pinned configuration.
 *
 * There is no default: leave them undefined for the normal runtime behavior
 * (any valid window in [8,15] / literal in [5,8]). Valid pinned values match
 * those ranges, e.g. -DTAMP_FIXED_WINDOW_BITS=10 -DTAMP_FIXED_LITERAL_BITS=8.
 * They may be set independently. */
/* #define TAMP_FIXED_WINDOW_BITS  10 */
/* #define TAMP_FIXED_LITERAL_BITS 8  */

/* TAMP_USE_MEMSET: Use libc memset (default: 1).
 * Set to 0 for environments without libc (e.g. MicroPython native modules).
 * When disabled, uses a volatile loop that prevents GCC from emitting a
 * memset call at the cost of inhibiting store coalescing. */
#ifndef TAMP_USE_MEMSET
#define TAMP_USE_MEMSET 1
#endif

#if TAMP_USE_MEMSET
#include <string.h>
#define TAMP_MEMSET(dst, val, n) memset((dst), (val), (n))
#else
#define TAMP_MEMSET(dst, val, n)                                                     \
    do {                                                                             \
        volatile unsigned char *_tamp_p = (volatile unsigned char *)(dst);           \
        for (size_t _tamp_i = 0; _tamp_i < (n); _tamp_i++) _tamp_p[_tamp_i] = (val); \
    } while (0)
#endif

/* TAMP_STATIC_CONST: declaration prefix for file-local read-only tables.
 * Normally `static const`, but drops `static` on Xtensa MicroPython native
 * modules, where `static const` rodata returns incorrect values.
 * See micropython/micropython#14429. */
#if defined(__XTENSA__) && defined(MICROPY_ENABLE_DYNRUNTIME)
#define TAMP_STATIC_CONST const
#else
#define TAMP_STATIC_CONST static const
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

/* Extended format support (RLE, extended match).
 * Enabled by default. Disable to save code size on minimal builds.
 *
 * TAMP_EXTENDED is the master switch (default: 1).
 * TAMP_EXTENDED_COMPRESS and TAMP_EXTENDED_DECOMPRESS default to TAMP_EXTENDED,
 * but can be individually overridden for compressor-only or decompressor-only builds.
 */
#ifndef TAMP_EXTENDED
#define TAMP_EXTENDED 1
#endif
#ifndef TAMP_EXTENDED_DECOMPRESS
#define TAMP_EXTENDED_DECOMPRESS TAMP_EXTENDED
#endif
#ifndef TAMP_EXTENDED_COMPRESS
#define TAMP_EXTENDED_COMPRESS TAMP_EXTENDED
#endif

/* Extended encoding constants */
#if TAMP_EXTENDED_DECOMPRESS || TAMP_EXTENDED_COMPRESS
#define TAMP_RLE_SYMBOL 12
#define TAMP_EXTENDED_MATCH_SYMBOL 13
#define TAMP_LEADING_EXTENDED_MATCH_BITS 3
#define TAMP_LEADING_RLE_BITS 4
#define TAMP_RLE_MAX_WINDOW 8
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

    /* [100, 127] and [-128, -100] are reserved for user-defined callback codes.
     * tamp_callback_t returns are truncated to int8_t; use these ranges to
     * avoid collisions with library status codes. */
};
typedef int8_t tamp_res;

typedef struct TampConf {
    uint16_t window : 4;                 // number of window bits
    uint16_t literal : 4;                // number of literal bits
    uint16_t use_custom_dictionary : 1;  // Use a custom initialized dictionary.
    uint16_t extended : 1;               // Extended format (RLE, extended match). Read from header bit [1].
    uint16_t dictionary_reset : 1;  // Stream may contain double-FLUSH dictionary resets. Implied by header byte 1 bit
                                    // [0] (more_header).
    uint16_t append : 1;            // Initialize for appending to an existing stream (FLUSH instead of header).
#if TAMP_LAZY_MATCHING
    uint16_t lazy_matching : 1;  // use Lazy Matching (spend 50-75% more CPU for around 0.5-2.0% better compression.)
                                 // only effects compression operations.
#endif
} TampConf;

/**
 * User-provided callback to be invoked periodically by the higher-level API.
 *
 * The callback fires once per compression/decompression cycle (i.e., once per
 * encoded or decoded token). For the stream API, it fires once per read-chunk.
 *
 * In all contexts, bytes_processed tracks input bytes consumed and total_bytes
 * is the total input size (or 0 if unknown). This allows computing a meaningful
 * progress percentage as bytes_processed / total_bytes.
 *
 * Non-stream API (total_bytes is known):
 *   tamp_compressor_compress_cb:   (input_consumed, total_input_size)
 *   tamp_decompressor_decompress_cb: (input_consumed, total_input_size)
 *
 * Stream API (total_bytes is 0; input size unknown):
 *   tamp_compress_stream:   (input_consumed, 0)
 *   tamp_decompress_stream: (input_consumed, 0)
 *
 * @param[in,out] user_data Arbitrary user-provided data.
 * @param[in] bytes_processed Input bytes consumed so far.
 * @param[in] total_bytes Total input size, or 0 if unknown (stream API).
 *
 * @return 0 to continue, or non-zero to abort. The return value is truncated
 *         to tamp_res (int8_t) and propagated to the caller. Use values in
 *         [100, 127] or [-128, -100] for custom codes to avoid collisions.
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
 * Uses a per-literal-size seed table so the dictionary only contains bytes that
 * are valid and useful for the given configuration:
 *   - literal=7,8: common english text/markup characters (" \0 0 e i > t o < a n s \\n r / .")
 *   - literal=5,6: common english letters (" etaoinshrdlcumw") downshifted to the target bit width
 *
 * For v1 backwards compatibility, callers should pass literal=8 when the
 * extended header flag is not set, regardless of the configured literal value.
 *
 * @param[out] buffer Populated output buffer.
 * @param[in] size Size of output buffer in bytes.
 * @param[in] literal Number of literal bits (5-8). Selects the appropriate seed character table.
 */
void tamp_initialize_dictionary(unsigned char *buffer, size_t size, uint8_t literal);

/**
 * @brief Compute the minimum viable pattern size given window and literal config parameters.
 *
 * @param[in] window Number of window bits. Valid values are [8, 15].
 * @param[in] literal Number of literal bits. Valid values are [5, 8].
 *
 * @return The minimum pattern size in bytes. Either 2 or 3.
 */
int8_t tamp_compute_min_pattern_size(uint8_t window, uint8_t literal);

/**
 * @brief Copy pattern from window to window, updating window_pos.
 *
 * Handles potential overlap between source and destination regions by
 * copying backwards when the destination would "catch up" to the source.
 *
 * IMPORTANT: Caller must validate that (window_offset + match_size) does not
 * exceed window bounds before calling this function. This function assumes
 * window_offset and match_size are pre-validated and does not perform
 * bounds checking on source reads.
 *
 * @param window Circular buffer (size must be power of 2)
 * @param window_pos Current write position (updated by this function)
 * @param window_offset Source position to copy from
 * @param match_size Number of bytes to copy
 * @param window_mask Bitmask for wrapping (window_size - 1)
 */
void tamp_window_copy(unsigned char *window, uint16_t *window_pos, uint16_t window_offset, uint8_t match_size,
                      uint16_t window_mask);

#ifdef __cplusplus
}
#endif

#endif
