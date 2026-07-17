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

#include "decompressor_fuzz_case.h"
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
        tamp_fuzz_case_run(entry_buf, len, buffers.window, buffers.output, sizeof(buffers.output));
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
