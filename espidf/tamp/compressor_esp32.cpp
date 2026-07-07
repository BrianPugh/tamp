#include "common.h"

/* Modification of the original tamp compressor.c, 2024 <https://github.com/BitsForPeople> */

#if TAMP_ESP32

#include <cstring>

#include "compressor.h"

#if TAMP_ESP32_AUTO_RESET_TASK_WDT
#include "esp_task_wdt.h"
#endif

#include "private/copyutil.hpp"
#include "private/tamp_search.hpp"

extern "C" {

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

/* Longest pattern findable by find_best_match. Must agree with MAX_PATTERN_SIZE in compressor.c:
 * in extended mode the limit there (min_pattern_size + 131) exceeds the 16-byte input buffer,
 * so the input buffer is the effective cap. */
#if TAMP_EXTENDED_COMPRESS
#define MAX_PATTERN_SIZE \
    (compressor->conf.extended ? (uint32_t)sizeof(compressor->input) : (uint32_t)(compressor->min_pattern_size + 13u))
#else
#define MAX_PATTERN_SIZE ((uint32_t)(compressor->min_pattern_size + 13u))
#endif
#define WINDOW_SIZE (1 << compressor->conf.window)

static inline void auto_reset_task_wdt(void) {
#if TAMP_ESP32_AUTO_RESET_TASK_WDT
    /* esp_task_wdt_reset() does a task-list lookup inside a critical section; on small
     * windows that is a measurable fraction of a search. Every 64th call is at most
     * ~1KB of consumed input between resets - far below any practical WDT timeout. */
    static uint32_t call_count;
    if (TAMP_UNLIKELY((call_count++ & 0x3F) == 0)) esp_task_wdt_reset();
#endif
}

// Object to hold a temporary copy of input data (also enforcing alignment)
class InputCopy {
   public:
    InputCopy(const TampCompressor &compressor) noexcept : compressor{compressor} { (void)input; }

    /**
     * @brief Returns a pointer to a sequence of at least \p minBytes bytes from the compressor's
     * current input buffer. Makes a copy of the bytes and returns a pointer into \c this->input if
     * necessary.
     *
     * @param minBytes minimum amount of sequential bytes wanted
     * @return pointer to a sequence of input bytes, either in \c compressor.input or to the copy in
     * \c this->input.
     */
    const uint8_t *getInput(const uint32_t minBytes) noexcept {
        constexpr uint32_t INBUF_SIZE = sizeof(TampCompressor::input);

        const uint32_t ipos = compressor.input_pos;
        if (ipos == 0) {
            // Nothing to be done.
            return compressor.input;
        }

        if constexpr (tamp::Arch::ESP32S3 && INBUF_SIZE == 16) {
            // Rotate 16 bytes from compressor.input 'right' (down) by ipos bytes and
            // store in this->input.

            asm volatile(
                // Load
                "EE.LD.128.USAR.IP q0, %[input], 16"
                "\n"
                "EE.VLD.128.IP q1, %[input], -16"
                "\n"
                // Align
                "EE.SRC.Q q0, q0, q1"
                "\n"
                // Rotate
                "WUR.SAR_BYTE %[shift]"
                "\n"
                "EE.SRC.Q q0, q0, q0"
                "\n"
                // Store
                "EE.VST.128.IP q0, %[out], 0"
                "\n"
                : "=m"(this->input)
                : [input] "r"(compressor.input), [shift] "r"(ipos), [out] "r"(this->input), "m"(compressor.input));

            return this->input;

        } else {
            const uint32_t l1 = INBUF_SIZE - ipos;
            if (minBytes <= l1) {
                // Nothing to be done.
                return compressor.input + ipos;
            } else {
                mem::cpy_short<INBUF_SIZE>(this->input, compressor.input + ipos, l1);
                mem::cpy_short<INBUF_SIZE>(this->input + l1, compressor.input, minBytes - l1);
                return this->input;
            }
        }
    }

   private:
    alignas(16) uint8_t input[sizeof(TampCompressor::input)];
    const TampCompressor &compressor;
};

void find_best_match(TampCompressor *compressor, uint16_t *match_index, uint8_t *match_size) {
    tamp::byte_span out{};

    auto_reset_task_wdt();

    if (TAMP_LIKELY(compressor->input_size >= compressor->min_pattern_size)) {
        const uint32_t patLen = MIN(compressor->input_size, MAX_PATTERN_SIZE);

        /* We need the pattern to match to be sequential in memory.
           If the current pattern wraps around in the input buffer, we make a
           straightened-out copy to use for the search. If not, we use the
           input data directly.
        */

        InputCopy input{*compressor};

        out = tamp::Locator::find_longest_match(input.getInput(patLen), patLen, compressor->window, WINDOW_SIZE);
    }
    *match_size = out.size();
    // out.data() is nullptr on the no-match path; subtracting from it is UB.
    *match_index = out.empty() ? 0 : out.data() - compressor->window;
}

#if TAMP_EXTENDED_COMPRESS

/* Longest extended match. Must agree with MAX_PATTERN_SIZE_EXTENDED in compressor.c:
 * min_pattern_size + 11 + EXTENDED_MATCH_MAX_EXTRA. */
#define MAX_EXTENDED_PATTERN_SIZE ((uint32_t)compressor->min_pattern_size + 11u + ((14u << 3) + 7u + 1u))
/* Compile-time bound of the above: min_pattern_size is at most 3. */
constexpr uint32_t MAX_EXTENDED_PATTERN_SIZE_BOUND = 3 + 11 + ((14 << 3) + 7 + 1);

/**
 * @brief ESP32-optimized replacement for the scalar find_extended_match in compressor.c.
 *
 * Searches window[current_pos...] for the longest occurrence of the implicit pattern
 * window[current_pos : current_pos + current_count] + input[0...]. Same contract as the
 * scalar version (see compressor.c), except a different (equally long) match position
 * may be returned on ties.
 */
void find_extended_match(TampCompressor *compressor, uint16_t current_pos, uint8_t current_count, uint16_t *new_pos,
                         uint8_t *new_count) {
    // Preconditions (guaranteed by caller):
    // - input_size > 0
    // - current_pos + current_count < WINDOW_SIZE
    // - current_count < MAX_EXTENDED_PATTERN_SIZE

    auto_reset_task_wdt();

    const uint32_t max_pattern = MIN((uint32_t)current_count + compressor->input_size, MAX_EXTENDED_PATTERN_SIZE);
    const uint32_t input_bytes = max_pattern - current_count;

    /* The pattern to search for is split between the window (the match so far, contiguous
     * by precondition) and the input ring buffer; straighten it into one buffer. */
    alignas(4) uint8_t pattern[MAX_EXTENDED_PATTERN_SIZE_BOUND];
    InputCopy input{*compressor};
    std::memcpy(pattern, compressor->window + current_pos, current_count);
    std::memcpy(pattern + current_count, input.getInput(input_bytes), input_bytes);

    // LONG_VERIFY: candidates share a long (current_count) prefix with the pattern, so
    // verification compares are long and word-wise compare wins.
    tamp::byte_span out = tamp::Locator::find_longest_match<true>(
        pattern, max_pattern, compressor->window + current_pos, WINDOW_SIZE - current_pos, (uint32_t)current_count + 1);

    *new_count = out.size();
    if (out.size()) *new_pos = out.data() - compressor->window;
}

#endif  // TAMP_EXTENDED_COMPRESS

}  // extern "C"

#endif  // TAMP_ESP32
