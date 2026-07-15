#ifndef TAMP_BENCH_H
#define TAMP_BENCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shared on-device benchmark/test harness. Standard workload: compress and
 * decompress the first 100 KB of enwik8 with a 10-bit window, verify against
 * a v1 (non-extended) reference, replay packed regression vectors, and run
 * seeded PRNG round-trip stress. Each device implements tamp_bench_time_us()
 * and calls tamp_bench_run() from a thin platform main.
 *
 * Fixed assumptions (not configurable via TampBenchData): input_size <=
 * 100000 (static buffer ceiling, ~164 KB total RAM), enwik8 window = 10 bits,
 * stress windows {8,10,12} on 8 KB blocks, and the synthetic/repetitive
 * blocks always process their full fixed sizes. Vector container is
 * little-endian (all supported targets are LE). */

typedef struct {
    const uint8_t *input; /* original data (first 100 KB of enwik8) */
    size_t input_size;
    const uint8_t *reference; /* expected compressed output (v1 format, window=10) */
    size_t reference_size;
    const uint8_t *vectors; /* packed vectors (tools/pack-device-vectors.py); NULL to skip */
    size_t vectors_size;
    int stress_iterations; /* per window/generator combination; 0 selects the default (10) */
} TampBenchData;

/* Provided by each device port. */
uint64_t tamp_bench_time_us(void);

/* Runs all benchmark/verification blocks, prints the TAMP-DEVICE-RESULT
 * sentinel, and returns the failure count. */
int tamp_bench_run(const TampBenchData *data);

#ifdef __cplusplus
}
#endif

#endif
