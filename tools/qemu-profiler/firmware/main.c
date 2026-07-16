/* Decompression benchmark firmware for QEMU MPS2 machines.
 *
 * Mirrors the devices/ benchmark workload: one-shot full-buffer decompress of
 * a compressed enwik8 blob (header-configured, window=10), verified against
 * the original bytes. The QEMU insncount plugin provides the metric; this
 * firmware only has to do the work and report pass/fail via semihosting.
 *
 * BENCH_DATA_H selects the input/reference arrays (classic vs extended blob).
 */
#include <stdint.h>
#include <string.h>

#include "tamp/decompressor.h"

#ifndef BENCH_DATA_H
#define BENCH_DATA_H "bench_data_classic.h"
#endif
#include BENCH_DATA_H

#ifndef WINDOW_BITS
#define WINDOW_BITS 10
#endif

#ifndef N_RUNS
#define N_RUNS 1
#endif

void semihost_puts(const char *s);
void semihost_put_u32(uint32_t v);

static unsigned char window_buffer[1 << WINDOW_BITS];
static unsigned char output_buffer[BENCH_OUTPUT_SIZE];

int main(void) {
    for (int run = 0; run < N_RUNS; run++) {
        TampDecompressor decompressor;
        size_t output_written_size = 0, input_consumed_size = 0;
        tamp_res res = tamp_decompressor_init(&decompressor, NULL, window_buffer, WINDOW_BITS);
        if (res != TAMP_OK) {
            semihost_puts("QEMU-BENCH: FAIL init\n");
            return 1;
        }
        res = tamp_decompressor_decompress(&decompressor, output_buffer, sizeof(output_buffer), &output_written_size,
                                           BENCH_INPUT, BENCH_INPUT_SIZE, &input_consumed_size);
        if (res < TAMP_OK) {
            semihost_puts("QEMU-BENCH: FAIL decompress res\n");
            return 1;
        }
        if (output_written_size != BENCH_EXPECTED_SIZE) {
            semihost_puts("QEMU-BENCH: FAIL size got=");
            semihost_put_u32((uint32_t)output_written_size);
            semihost_puts(" want=");
            semihost_put_u32((uint32_t)BENCH_EXPECTED_SIZE);
            semihost_puts("\n");
            return 1;
        }
        if (memcmp(output_buffer, BENCH_EXPECTED, BENCH_EXPECTED_SIZE) != 0) {
            semihost_puts("QEMU-BENCH: FAIL content\n");
            return 1;
        }
    }
    semihost_puts("QEMU-BENCH: PASS runs=");
    semihost_put_u32(N_RUNS);
    semihost_puts(" out_bytes=");
    semihost_put_u32((uint32_t)BENCH_EXPECTED_SIZE);
    semihost_puts("\n");
    return 0;
}
