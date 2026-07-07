/**
 * Host-side differential tester for the ESP32-optimized match finders
 * (espidf/tamp/compressor_esp32.cpp, built with TAMP_ESP32=1).
 *
 * On non-Xtensa hosts all inline-asm paths in tamp_search.hpp are compiled
 * out (if constexpr), so this exercises the scalar C++ search paths - the
 * same algorithm skeleton the SIMD paths feed into. Build with ASan/UBSan.
 *
 * For randomly generated compressor states, it checks:
 *   - find_best_match: match length equals an exhaustive reference search,
 *     and the returned match is in-bounds and byte-for-byte valid.
 *   - find_extended_match: match length equals the scalar reference
 *     implementation (from compressor.c), and the returned match is
 *     in-bounds and byte-for-byte valid.
 *
 * Deterministic (fixed seed); exits non-zero on the first mismatch.
 */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

#include "tamp/compressor.h"

extern "C" void find_best_match(TampCompressor *compressor, uint16_t *match_index, uint8_t *match_size);
extern "C" void find_extended_match(TampCompressor *compressor, uint16_t current_pos, uint8_t current_count,
                                    uint16_t *new_pos, uint8_t *new_count);

namespace {

std::mt19937 rng(0x7a3f11u);

uint32_t rnd(uint32_t bound) { return rng() % bound; }

uint8_t ring(const TampCompressor *c, uint32_t off) { return c->input[(c->input_pos + off) & 0xF]; }

uint32_t window_size(const TampCompressor *c) { return 1u << c->conf.window; }

/* Pattern-length cap; must agree with MAX_PATTERN_SIZE in compressor.c /
 * compressor_esp32.cpp. */
uint32_t best_match_cap(const TampCompressor *c) {
    return c->conf.extended ? (uint32_t)sizeof(c->input) : (uint32_t)(c->min_pattern_size + 13);
}

#define MAX_EXTENDED_PATTERN(c) ((uint32_t)(c)->min_pattern_size + 11u + ((14u << 3) + 7u + 1u))

/* Exhaustive reference for find_best_match: longest match of the input ring
 * within the window, capped like the real implementations. */
void ref_find_best_match(const TampCompressor *c, uint16_t *match_index, uint8_t *match_size) {
    *match_size = 0;
    if (c->input_size < c->min_pattern_size) return;
    const uint32_t wsize = window_size(c);
    const uint32_t max_pattern = std::min<uint32_t>(c->input_size, best_match_cap(c));
    for (uint32_t idx = 0; idx + 2 <= wsize; idx++) {
        const uint32_t lim = std::min<uint32_t>(max_pattern, wsize - idx);
        uint32_t len = 0;
        while (len < lim && c->window[idx + len] == ring(c, len)) len++;
        if (len >= 2 && len > *match_size) {
            *match_size = (uint8_t)len;
            *match_index = (uint16_t)idx;
            if (len == max_pattern) return;
        }
    }
}

/* Scalar reference for find_extended_match: verbatim port of the !TAMP_ESP32
 * implementation in compressor.c. */
void ref_find_extended_match(const TampCompressor *c, uint16_t current_pos, uint8_t current_count, uint16_t *new_pos,
                             uint8_t *new_count) {
    *new_count = 0;
    const unsigned char *window = c->window;
    const uint32_t wsize = window_size(c);
    const uint8_t max_pattern = (uint8_t)std::min<uint32_t>(current_count + c->input_size, MAX_EXTENDED_PATTERN(c));
    const uint8_t extend_byte = ring(c, 0);

    for (uint32_t cand = current_pos; cand + current_count + 1 <= wsize; cand++) {
        if (window[cand + current_count] != extend_byte) continue;

        uint8_t i = 0;
        while (i < current_count && window[cand + i] == window[current_pos + i]) i++;
        if (i < current_count) continue;

        const uint8_t cand_max = (uint8_t)std::min<uint32_t>(max_pattern, wsize - cand);
        uint8_t match_len = current_count + 1;
        for (i = current_count + 1; i < cand_max; i++) {
            if (window[cand + i] != ring(c, i - current_count)) break;
            match_len = i + 1;
        }

        if (match_len > *new_count) {
            *new_count = match_len;
            *new_pos = (uint16_t)cand;
            if (match_len == max_pattern) return;
        }
    }
}

int failures = 0;

void fail(const char *what, const TampCompressor *c, uint32_t it) {
    fprintf(stderr, "FAIL %s: window_bits=%u extended=%u min_pattern=%u input_size=%u input_pos=%u iter=%u\n", what,
            c->conf.window, c->conf.extended, c->min_pattern_size, c->input_size, c->input_pos, it);
    failures++;
}

void fill_random(uint8_t *buf, uint32_t n, uint32_t alphabet) {
    for (uint32_t i = 0; i < n; i++) buf[i] = (uint8_t)rnd(alphabet);
}

void setup_state(TampCompressor *c, uint8_t *window, uint8_t window_bits) {
    memset(c, 0, sizeof(*c));
    c->window = window;
    c->conf.window = window_bits;
    c->conf.literal = 8;
    c->conf.extended = rng() & 1;
    c->min_pattern_size = 2 + (rng() & 1);
}

void test_find_best_match(uint8_t window_bits, uint32_t iters) {
    const uint32_t wsize = 1u << window_bits;
    uint8_t *window = (uint8_t *)malloc(wsize);  // exact size: ASan guards both ends
    if (!window) abort();

    for (uint32_t it = 0; it < iters; it++) {
        TampCompressor c;
        setup_state(&c, window, window_bits);
        c.input_size = 2 + rnd(15);  // 2..16
        c.input_pos = rnd(16);

        const uint32_t alphabet = 2u << rnd(5);  // 2..32
        fill_random(window, wsize, alphabet);
        fill_random(c.input, sizeof(c.input), alphabet);

        if (rng() & 1) {
            // Copy a window slice into the input ring to force matches.
            const uint32_t n = 2 + rnd(15);
            const uint32_t src = rnd(wsize - n);
            for (uint32_t i = 0; i < n; i++) c.input[(c.input_pos + i) & 0xF] = window[src + i];
        }
        if (rnd(4) == 0) {
            // Boundary stress: input prefix at the very end of the window
            // (regression for the find_longest_match window-end clamp).
            const uint32_t n = 2 + rnd(std::min<uint32_t>(c.input_size, 14) - 1);
            for (uint32_t i = 0; i < n; i++) window[wsize - n + i] = ring(&c, i);
        }

        uint16_t idx = 0, ref_idx = 0;
        uint8_t size = 0, ref_size = 0;
        find_best_match(&c, &idx, &size);
        ref_find_best_match(&c, &ref_idx, &ref_size);

        if (size != ref_size) {
            fail("find_best_match length mismatch", &c, it);
            fprintf(stderr, "  esp32 size=%u idx=%u, ref size=%u idx=%u\n", size, idx, ref_size, ref_idx);
        }
        if (size > 0) {
            if ((uint32_t)idx + size > wsize) fail("find_best_match match exceeds window", &c, it);
            for (uint32_t i = 0; i < size && (uint32_t)idx + i < wsize; i++) {
                if (window[idx + i] != ring(&c, i)) {
                    fail("find_best_match match bytes wrong", &c, it);
                    break;
                }
            }
        }
        if (failures) return;
    }
    free(window);
}

void test_find_extended_match(uint8_t window_bits, uint32_t iters) {
    const uint32_t wsize = 1u << window_bits;
    uint8_t *window = (uint8_t *)malloc(wsize);
    if (!window) abort();

    for (uint32_t it = 0; it < iters; it++) {
        TampCompressor c;
        setup_state(&c, window, window_bits);
        c.conf.extended = 1;
        c.input_size = 1 + rnd(16);  // 1..16
        c.input_pos = rnd(16);

        const uint32_t alphabet = 2u << rnd(4);  // 2..16
        fill_random(window, wsize, alphabet);
        fill_random(c.input, sizeof(c.input), alphabet);

        // current_count: mostly realistic (>= min+12), sometimes tiny
        const uint32_t cc_cap = std::min<uint32_t>(c.min_pattern_size + 130u, wsize - 2);
        uint32_t cc = (rng() & 1) ? (c.min_pattern_size + 12 + rnd(60)) : (1 + rnd(20));
        if (cc > cc_cap) cc = cc_cap;
        const uint32_t cp = rnd(wsize - cc);  // cp + cc < wsize (need +1 slack for search)
        if (cp + cc + 1 > wsize) continue;

        if (rng() & 1) {
            // Plant a continuation: copy window[cp..cp+cc) elsewhere and put
            // its continuation bytes into the input ring.
            const uint32_t ext = 1 + rnd(c.input_size);
            if (cp + 1 < wsize - cc - ext) {
                const uint32_t p2 = cp + 1 + rnd(wsize - cc - ext - cp - 1);
                memmove(window + p2, window + cp, cc);
                for (uint32_t i = 0; i < ext; i++) c.input[(c.input_pos + i) & 0xF] = window[p2 + cc + i];
            }
        }

        uint16_t np = 0, ref_np = 0;
        uint8_t nc = 0, ref_nc = 0;
        find_extended_match(&c, (uint16_t)cp, (uint8_t)cc, &np, &nc);
        ref_find_extended_match(&c, (uint16_t)cp, (uint8_t)cc, &ref_np, &ref_nc);

        if (nc != ref_nc) {
            fail("find_extended_match length mismatch", &c, it);
            fprintf(stderr, "  cp=%u cc=%u: esp32 nc=%u np=%u, ref nc=%u np=%u\n", cp, cc, nc, np, ref_nc, ref_np);
        }
        if (nc > 0) {
            bool ok = (uint32_t)np + nc <= wsize && nc >= cc + 1 && np >= cp &&
                      nc <= std::min<uint32_t>(cc + c.input_size, MAX_EXTENDED_PATTERN(&c));
            for (uint32_t i = 0; ok && i < cc; i++) ok = window[np + i] == window[cp + i];
            for (uint32_t i = cc; ok && i < nc; i++) ok = window[np + i] == ring(&c, i - cc);
            if (!ok) fail("find_extended_match invalid match", &c, it);
        }
        if (failures) return;
    }
    free(window);
}

}  // namespace

int main() {
    // Big windows have an O(window * pattern) reference; scale iterations down.
    test_find_best_match(8, 20000);
    test_find_best_match(10, 8000);
    test_find_best_match(12, 2000);
    test_find_best_match(15, 300);

    test_find_extended_match(8, 20000);
    test_find_extended_match(10, 8000);
    test_find_extended_match(12, 2000);
    test_find_extended_match(15, 300);

    if (failures) {
        fprintf(stderr, "esp32 host differential: FAILED\n");
        return 1;
    }
    printf("esp32 host differential: all checks passed\n");
    return 0;
}
