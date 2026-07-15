#ifndef TAMP_PROFILING_H
#define TAMP_PROFILING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Timestamp source implemented by each device port (see tamp_bench.h). */
uint64_t tamp_bench_time_us(void);

typedef struct {
    // Counters
    uint32_t literal_count;
    uint32_t pattern_count;
    uint32_t pattern_bytes_total;
    uint32_t overlap_count;      // Times src/dst regions overlap
    uint32_t non_overlap_count;  // Times they don't overlap

    // Timing (microseconds)
    uint64_t literal_time_us;
    uint64_t pattern_decode_time_us;   // Token decoding (huffman + offset)
    uint64_t pattern_output_time_us;   // Copy pattern to output buffer
    uint64_t window_update_time_us;    // The tmp_buf copy loop (optimization target)
    uint64_t bit_buffer_fill_time_us;  // Filling the bit buffer from input
} TampProfilingStats;

extern TampProfilingStats tamp_profiling_stats;

static inline void tamp_profiling_reset(void) {
    tamp_profiling_stats.literal_count = 0;
    tamp_profiling_stats.pattern_count = 0;
    tamp_profiling_stats.pattern_bytes_total = 0;
    tamp_profiling_stats.overlap_count = 0;
    tamp_profiling_stats.non_overlap_count = 0;
    tamp_profiling_stats.literal_time_us = 0;
    tamp_profiling_stats.pattern_decode_time_us = 0;
    tamp_profiling_stats.pattern_output_time_us = 0;
    tamp_profiling_stats.window_update_time_us = 0;
    tamp_profiling_stats.bit_buffer_fill_time_us = 0;
}

// Timing macros
#define TAMP_PROFILE_START() uint64_t _prof_start = tamp_bench_time_us()
#define TAMP_PROFILE_END(accumulator) (accumulator) += (tamp_bench_time_us() - _prof_start)

// For nested timing (when we need multiple timers)
#define TAMP_PROFILE_START_NAMED(name) uint64_t _prof_start_##name = tamp_bench_time_us()
#define TAMP_PROFILE_END_NAMED(name, accumulator) (accumulator) += (tamp_bench_time_us() - _prof_start_##name)

#ifdef __cplusplus
}
#endif

#endif
