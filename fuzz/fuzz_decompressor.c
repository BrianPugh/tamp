/**
 * Fuzz harness: feed arbitrary bytes to the decompressor.
 *
 * This is the highest-priority target since decompressors typically
 * handle untrusted input. We test across all valid window/literal
 * configurations, with and without extended format, and across
 * fuzz-chosen input/output chunk sizes so the suspend/resume state
 * machines (mid-token INPUT_EXHAUSTED, output-full skip_bytes /
 * token_state resume, header-byte stash) run against malicious data,
 * not just the happy full-buffer path.
 *
 * Build with the portable defaults AND with the ARMV7EM-profile flags
 * (TAMP_FAST_DECODE_LOOP=1 TAMP_WINDOW_FROM_OUTPUT=1 TAMP_FAST_WINDOW_COPY=1
 *  TAMP_FAST_BIT_REFILL=1 TAMP_FAST_OUTPUT_COPY=1) - the fast decode loop and
 * the inline window-update variants are compiled out otherwise and would
 * never be fuzzed.
 */
#include "decompressor_fuzz_case.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Corpus byte contract + drive loop live in decompressor_fuzz_case.h,
    // shared with the QEMU replay firmware so the encoding cannot diverge.
    unsigned char window[1 << 15];  // Max window size
    unsigned char output[4096];
    return tamp_fuzz_case_run(data, size, window, output, sizeof(output));
}
