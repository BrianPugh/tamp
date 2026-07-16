/* Adversarial corpus replay firmware for QEMU MPS2 machines.
 *
 * Mirrors fuzz/fuzz_decompressor.c (same config/chunk-byte scheme), but runs
 * the REAL ARM binary on the emulated core: this catches ISA/ABI-specific
 * defects host fuzzing cannot (codegen at -O3 for the target, unsigned plain
 * char, 32-bit size_t). No ASAN here, so the window and output buffers are
 * fenced with canaries checked after every entry, and faults report via the
 * vector handlers in startup.c.
 *
 * Input: a single packed blob read via semihosting file I/O from the host:
 *   u32 count, then per entry { u32 len, u8 data[len] }  (little-endian)
 * Entries larger than ENTRY_CAP are skipped (read and discarded).
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "tamp/decompressor.h"

#ifndef REPLAY_BLOB_PATH
#define REPLAY_BLOB_PATH "build/qemu-corpus.bin"
#endif

#define ENTRY_CAP 65536
#define CANARY 0xC5
#define CANARY_LEN 64

void semihost_puts(const char *s);
void semihost_put_u32(uint32_t v);
int semihost_open_rb(const char *path);
int semihost_read(int handle, void *buf, uint32_t len); /* returns bytes read */
void semihost_close(int handle);

static const uint16_t CHUNK_SIZES[16] = {1, 2, 3, 4, 5, 7, 8, 16, 17, 31, 32, 33, 64, 241, 256, 4096};

static unsigned char entry_buf[ENTRY_CAP];

static struct {
    unsigned char pre[CANARY_LEN];
    unsigned char window[1 << 15];
    unsigned char mid[CANARY_LEN];
    unsigned char output[4096];
    unsigned char post[CANARY_LEN];
} buffers;

static void fence_reset(void) {
    memset(buffers.pre, CANARY, sizeof(buffers.pre));
    memset(buffers.mid, CANARY, sizeof(buffers.mid));
    memset(buffers.post, CANARY, sizeof(buffers.post));
}

static bool fence_intact(void) {
    for (unsigned i = 0; i < CANARY_LEN; i++) {
        if (buffers.pre[i] != CANARY || buffers.mid[i] != CANARY || buffers.post[i] != CANARY) return false;
    }
    return true;
}

/* Same semantics as fuzz/fuzz_decompressor.c's LLVMFuzzerTestOneInput. */
static void run_one(const unsigned char *data, uint32_t size) {
    if (size < 2) return;
    uint8_t config_byte = data[0];
    uint8_t chunk_byte = data[1];
    data += 2;
    size -= 2;

    uint8_t window_bits = 8 + (config_byte & 0x07);
    uint8_t literal_bits = 5 + ((config_byte >> 3) & 0x03);
    bool extended = (config_byte >> 5) & 1;
    bool use_header = (config_byte >> 6) & 1;
    bool custom_dictionary = (config_byte >> 7) & 1;

    size_t out_chunk = CHUNK_SIZES[chunk_byte & 0x0F];
    size_t in_chunk = CHUNK_SIZES[(chunk_byte >> 4) & 0x0F];
    if (out_chunk > sizeof(buffers.output)) out_chunk = sizeof(buffers.output);

    TampDecompressor decompressor;
    tamp_res res;

    if (custom_dictionary) memset(buffers.window, 0xA5, sizeof(buffers.window));

    if (use_header) {
        res = tamp_decompressor_init(&decompressor, NULL, buffers.window, 15);
    } else {
        TampConf conf = {
            .window = window_bits,
            .literal = literal_bits,
            .use_custom_dictionary = custom_dictionary,
            .extended = extended,
        };
        res = tamp_decompressor_init(&decompressor, &conf, buffers.window, window_bits);
    }
    if (res != TAMP_OK) return;

    const unsigned char *remaining = data;
    size_t remaining_size = size;
    int stalled = 0;

    while (remaining_size > 0) {
        size_t feed = remaining_size < in_chunk ? remaining_size : in_chunk;
        size_t consumed = 0;
        size_t written = 0;
        res = tamp_decompressor_decompress(&decompressor, buffers.output, out_chunk, &written, remaining, feed,
                                           &consumed);
        remaining += consumed;
        remaining_size -= consumed;

        if (res < 0 && res != TAMP_INPUT_EXHAUSTED) break;
        if (consumed == 0 && written == 0) {
            if (++stalled >= 2) break;
        } else {
            stalled = 0;
        }
        if (res == TAMP_INPUT_EXHAUSTED && consumed == 0 && feed == remaining_size) break;
    }
}

int main(void) {
    int fd = semihost_open_rb(REPLAY_BLOB_PATH);
    if (fd < 0) {
        semihost_puts("QEMU-REPLAY: FAIL open " REPLAY_BLOB_PATH "\n");
        return 1;
    }

    uint32_t count = 0;
    if (semihost_read(fd, &count, 4) != 4) {
        semihost_puts("QEMU-REPLAY: FAIL blob header\n");
        return 1;
    }

    fence_reset();
    uint32_t ran = 0, skipped = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t len = 0;
        if (semihost_read(fd, &len, 4) != 4) {
            semihost_puts("QEMU-REPLAY: FAIL blob truncated\n");
            return 1;
        }
        if (len > ENTRY_CAP) {
            /* Discard oversized entry. */
            uint32_t left = len;
            while (left) {
                uint32_t n = left > sizeof(entry_buf) ? sizeof(entry_buf) : left;
                if (semihost_read(fd, entry_buf, n) != (int)n) {
                    semihost_puts("QEMU-REPLAY: FAIL blob truncated\n");
                    return 1;
                }
                left -= n;
            }
            skipped++;
            continue;
        }
        if (semihost_read(fd, entry_buf, len) != (int)len) {
            semihost_puts("QEMU-REPLAY: FAIL blob truncated\n");
            return 1;
        }
        run_one(entry_buf, len);
        if (!fence_intact()) {
            semihost_puts("QEMU-REPLAY: FAIL canary after entry ");
            semihost_put_u32(i);
            semihost_puts("\n");
            return 1;
        }
        ran++;
    }
    semihost_close(fd);

    semihost_puts("QEMU-REPLAY: PASS ran=");
    semihost_put_u32(ran);
    semihost_puts(" skipped=");
    semihost_put_u32(skipped);
    semihost_puts("\n");
    return 0;
}
