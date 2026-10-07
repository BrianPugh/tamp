/**
 * @file match_index_check.c
 * @brief Checks that TAMP_MATCH_INDEX output is byte-identical to the linear-scan match finder.
 *
 * Compresses a fixed, seeded set of cases: random configs, adversarial data, small-chunk
 * streaming, custom dictionaries and mid-stream dictionary resets.
 *
 *   match_index_check          Compress each case with and without the index and compare
 *                              (requires TAMP_MATCH_INDEX=1). Exits non-zero on mismatch.
 *   match_index_check --dump   Write every case's compressed output to stdout, using the
 *                              index when compiled in. Diffing the dumps of an indexed build
 *                              and a TAMP_USE_EMBEDDED_MATCH=1 build compares the index against
 *                              the match finder that ships to devices.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tamp/compressor.h"

#define NUM_CASES 300
#define MAX_INPUT 30000

static unsigned long long rng_state;

static unsigned rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (unsigned)rng_state;
}

static void generate(unsigned char *buf, size_t n, int kind) {
    const unsigned char alphabet[4] = {(unsigned char)rnd(), (unsigned char)rnd(), (unsigned char)rnd(),
                                       (unsigned char)rnd()};
    for (size_t i = 0; i < n; i++) {
        switch (kind) {
            case 0:
                buf[i] = (unsigned char)rnd();
                break;
            case 1:
                buf[i] = 0;
                break;
            case 2:
                buf[i] = alphabet[rnd() & 1];
                break;
            case 3:
                buf[i] = alphabet[rnd() & 3];
                break;
            case 4:  // Short-range repeats.
                buf[i] = (i > 64 && rnd() % 8) ? buf[i - 1 - rnd() % 64] : (unsigned char)rnd();
                break;
            case 5:  // Short periodic patterns: chains span the window and full-length matches abound.
                buf[i] = (rnd() % 500) ? alphabet[(i % (2 + (n % 7))) & 3] : (unsigned char)rnd();
                break;
            case 6:  // Repeats from farther back than small windows reach.
                buf[i] = (i > 5000 && rnd() % 16) ? buf[i - 1 - rnd() % 5000] : (unsigned char)rnd();
                break;
            case 7:  // Mostly zeros with sparse noise, like an uncompressed binary diff.
                buf[i] = rnd() % 8 ? 0 : (rnd() % 4 ? (unsigned char)(rnd() % 4) : (unsigned char)rnd());
                break;
            default:  // Long runs mixed with noise.
                if (rnd() % 400 == 0) {
                    const unsigned char c = (unsigned char)rnd();
                    for (size_t run = rnd() % 600; run && i < n; run--) buf[i++] = c;
                    i--;
                } else {
                    buf[i] = (unsigned char)(rnd() % 10 ? i / 97 : rnd());
                }
                break;
        }
    }
}

/* Compress one case; returns the output length. stream_seed fixes chunking and reset points. */
static size_t compress_case(const unsigned char *in, size_t n, const TampConf *conf, int use_index,
                            unsigned stream_seed, unsigned char *out, size_t cap) {
    unsigned char *window = malloc((size_t)1 << conf->window);
    if (!window) abort();
    if (conf->use_custom_dictionary) {
        unsigned s = stream_seed;
        for (size_t i = 0; i < ((size_t)1 << conf->window); i++) {
            s = s * 1103515245u + 12345u;
            window[i] = (s >> 16) & 3 ? in[(s >> 8) % n] : (unsigned char)(s >> 20);
        }
    }
#if TAMP_MATCH_INDEX
    void *index = use_index ? malloc(TAMP_MATCH_INDEX_SIZE(conf->window)) : NULL;
    if (use_index && !index) abort();
#else
    (void)use_index;
#endif

    TampCompressor compressor;
    if (tamp_compressor_init(&compressor, conf, window) != TAMP_OK) abort();
#if TAMP_MATCH_INDEX
    if (index) tamp_compressor_set_match_index(&compressor, index);
#endif

    size_t total = 0, pos = 0;
    unsigned s = stream_seed;
    int resets = 0;
    while (pos < n) {
        s = s * 1103515245u + 12345u;
        size_t chunk = 1 + (s >> 16) % 3000;
        if (chunk > n - pos) chunk = n - pos;
        size_t consumed = 0, written = 0;
        if (tamp_compressor_compress(&compressor, out + total, cap - total, &written, in + pos, chunk, &consumed) < 0)
            abort();
        total += written;
        pos += consumed;
        if (conf->dictionary_reset && resets < 3 && ((s >> 8) % 37) == 0) {
            if (tamp_compressor_reset_dictionary(&compressor, out + total, cap - total, &written) < 0) abort();
            total += written;
            resets++;
#if TAMP_MATCH_INDEX
            if (index) tamp_compressor_set_match_index(&compressor, index);
#endif
        }
    }
    size_t written = 0;
    if (tamp_compressor_flush(&compressor, out + total, cap - total, &written, false) < 0) abort();
    total += written;

    free(window);
#if TAMP_MATCH_INDEX
    free(index);
#endif
    return total;
}

int main(int argc, char **argv) {
    const int dump = argc > 1 && strcmp(argv[1], "--dump") == 0;
#if !TAMP_MATCH_INDEX
    if (!dump) {
        fprintf(stderr, "match_index_check: build with TAMP_MATCH_INDEX=1, or pass --dump\n");
        return 2;
    }
#endif
    const size_t cap = MAX_INPUT * 3 + 1024;
    unsigned char *in = malloc(MAX_INPUT), *a = malloc(cap), *b = malloc(cap);
    if (!in || !a || !b) abort();

    int mismatches = 0;
    rng_state = 88172645463325252ULL;
    for (int i = 0; i < NUM_CASES; i++) {
        TampConf conf = {0};
        conf.window = 8 + rnd() % 8;
        conf.literal = 5 + rnd() % 4;
        conf.extended = TAMP_EXTENDED_COMPRESS ? (rnd() & 1) : 0;
#if TAMP_LAZY_MATCHING
        conf.lazy_matching = rnd() & 1;
#endif
        conf.use_custom_dictionary = (rnd() % 4) == 0;
        conf.dictionary_reset = !conf.use_custom_dictionary && (rnd() % 3) == 0;

        // Keeps the linear scan's O(n * window) cost reasonable under sanitizers.
        const size_t n = 1 + rnd() % (conf.window <= 10 ? MAX_INPUT : 6000);
        const int kind = rnd() % 9;
        generate(in, n, kind);
        if (conf.literal < 8)
            for (size_t j = 0; j < n; j++) in[j] &= (1 << conf.literal) - 1;
        const unsigned stream_seed = rnd();

        const size_t len_a = compress_case(in, n, &conf, 1, stream_seed, a, cap);
        if (dump) {
            fwrite(&len_a, sizeof(len_a), 1, stdout);
            fwrite(a, 1, len_a, stdout);
            continue;
        }
        const size_t len_b = compress_case(in, n, &conf, 0, stream_seed, b, cap);
        if (len_a != len_b || memcmp(a, b, len_a) != 0) {
            mismatches++;
            fprintf(stderr, "MISMATCH case=%d kind=%d n=%zu window=%u literal=%u extended=%u custom_dict=%u reset=%u\n",
                    i, kind, n, conf.window, conf.literal, conf.extended, conf.use_custom_dictionary,
                    conf.dictionary_reset);
        }
    }
    if (!dump) printf("match_index_check: %d cases, %d mismatches\n", NUM_CASES, mismatches);
    free(in);
    free(a);
    free(b);
    return mismatches != 0;
}
