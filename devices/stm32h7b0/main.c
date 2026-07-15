/* STM32H7B0 port of the shared tamp device harness.
 *
 * The 128K internal flash cannot hold the ~150K of benchmark data, so the
 * firmware is code-only: run.tcl loads a packed blob (tools/pack-device-blob.py)
 * into AXI SRAM at BLOB_ADDR over SWD before resuming the core. */
#include <stdint.h>
#include <stdio.h>

#include "tamp_bench.h"

#define BLOB_ADDR 0x24000000u
#define BLOB_MAGIC 0x504D4154u /* "TAMP" */

typedef struct {
    uint32_t magic;
    uint32_t input_size;
    uint32_t reference_size;
    uint32_t vectors_size;
    uint8_t payload[];
} BenchBlob;

void tamp_system_report(void);

int main(void) {
    /* One semihosting trap per line instead of per fwrite chunk. */
    static char stdout_buf[256];
    setvbuf(stdout, stdout_buf, _IOLBF, sizeof(stdout_buf));

    tamp_system_report();

    const BenchBlob *blob = (const BenchBlob *)BLOB_ADDR;
    if (blob->magic != BLOB_MAGIC) {
        printf("FAIL: no benchmark data blob at 0x%08x (magic 0x%08x) - run via run.tcl\n", (unsigned)BLOB_ADDR,
               (unsigned)blob->magic);
        printf("TAMP-DEVICE-RESULT: FAIL failures=1\n");
        fflush(stdout);
        for (;;)
            ;
    }

    TampBenchData data = {
        .input = blob->payload,
        .input_size = blob->input_size,
        .reference = blob->payload + blob->input_size,
        .reference_size = blob->reference_size,
        .vectors = blob->payload + blob->input_size + blob->reference_size,
        .vectors_size = blob->vectors_size,
        .stress_iterations = 0,
    };

    tamp_bench_run(&data);

    /* Single-shot: every run is started fresh by OpenOCD, and the runner
     * detaches once it sees the sentinel. */
    for (;;)
        ;
}
