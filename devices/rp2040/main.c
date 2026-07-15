#include "enwik8.h"
#include "enwik8_compressed.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "tamp_bench.h"
#include "vectors.h"

uint64_t tamp_bench_time_us(void) { return time_us_64(); }

int main() {
    stdio_init_all();

    TampBenchData data = {
        .input = ENWIK8,
        .input_size = sizeof(ENWIK8),
        .reference = ENWIK8_COMPRESSED,
        .reference_size = sizeof(ENWIK8_COMPRESSED),
        .vectors = VECTORS,
        .vectors_size = sizeof(VECTORS),
        .stress_iterations = 0,
    };

    /* Loop forever: flashing is manual (BOOTSEL) and USB CDC attach timing is
     * racy, so a late-attaching monitor still catches a full run. */
    while (true) {
        tamp_bench_run(&data);
        sleep_ms(2000);
    }
}
