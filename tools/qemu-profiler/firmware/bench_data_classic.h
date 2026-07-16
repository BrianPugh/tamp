/* Classic (v1, non-extended) enwik8-100KB blob, window=10 — the standard
 * devices/BENCHMARKS.md workload. Arrays reused from the rp2040 harness. */
#include "enwik8.h"            /* ENWIK8: raw first 100 KB of enwik8 */
#include "enwik8_compressed.h" /* ENWIK8_COMPRESSED: v1 format, w=10 */

#define BENCH_INPUT ENWIK8_COMPRESSED
#define BENCH_INPUT_SIZE sizeof(ENWIK8_COMPRESSED)
#define BENCH_EXPECTED ENWIK8
#define BENCH_EXPECTED_SIZE sizeof(ENWIK8)
#define BENCH_OUTPUT_SIZE 100000
