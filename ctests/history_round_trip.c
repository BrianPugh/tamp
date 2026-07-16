/* Standalone round-trip regression test for the fast-loop history-window mode
 * (TAMP_HISTORY_WINDOW, see decompressor.c) and its two subtle paths:
 *
 *   - the seam-crossing bail: a classic match whose ring source straddles
 *     window_pos has no contiguous output-history image and must fall back to
 *     the ring path. Small windows + periodic data (period swept around the
 *     window boundary) force these; the ad-hoc bring-up harness saw ~150 of
 *     them, all decoding correctly.
 *   - the history match copy reading from `output`, whose source aliases the
 *     destination buffer (regression: an output-derived expression fed straight
 *     into the byte-loop TAMP_COPY_TO_OUTPUT read+advanced `output` in one
 *     unsequenced expression; the fix snapshots the source pointer first).
 *
 * History mode only arms on classic (non-extended) streams, so every stream
 * here is compressed with conf.extended = false. The compressor writes a
 * header, so the decompressor is initialized with conf = NULL to read it back.
 *
 * No Unity dependency: self-checking main, deterministic (fixed PRNG seed),
 * prints a message and exits non-zero on the first mismatch. Built two ways by
 * `make c-test-history` (history flags on, and portable defaults off) so both
 * decoder paths are exercised under -fsanitize=address,undefined.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tamp/compressor.h"
#include "tamp/decompressor.h"

#define MAX_WINDOW_BITS 12
#define MAX_INPUT (4u * (1u << MAX_WINDOW_BITS) + 128u) /* window=12 worst case */

static unsigned char cwin[1u << MAX_WINDOW_BITS];
static unsigned char dwin[1u << MAX_WINDOW_BITS];
static unsigned char comp[MAX_INPUT * 2u + 256u];
static unsigned char deco[MAX_INPUT];
static unsigned char full[MAX_INPUT];

/* xorshift32 with a fixed seed: the whole test is reproducible. */
static uint32_t rng = 0x12345678u;
static uint32_t xr(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static const size_t OUT_CHUNKS[] = {sizeof(deco), 64, 65, 100, 4096};

/* Returns 0 on success, 1 on any failure (message already printed). */
static int roundtrip(const unsigned char *data, size_t n, uint8_t window) {
    TampConf conf;
    memset(&conf, 0, sizeof(conf));
    conf.window = window;
    conf.literal = 8;
    conf.use_custom_dictionary = false;
    conf.extended = false; /* classic -> history-window eligible */

    TampCompressor c;
    if (tamp_compressor_init(&c, &conf, cwin) != TAMP_OK) {
        printf("FAIL: compressor_init window=%u\n", window);
        return 1;
    }
    size_t cw = 0, ci = 0;
    tamp_res r = tamp_compressor_compress_and_flush(&c, comp, sizeof(comp), &cw, data, n, &ci, false);
    if (r != TAMP_OK || ci != n) {
        printf("FAIL: compress window=%u n=%zu res=%d consumed=%zu\n", window, n, r, ci);
        return 1;
    }

    for (size_t oc = 0; oc < sizeof(OUT_CHUNKS) / sizeof(OUT_CHUNKS[0]); oc++) {
        size_t ochunk = OUT_CHUNKS[oc];
        if (ochunk > sizeof(deco)) ochunk = sizeof(deco);

        TampDecompressor d;
        /* conf = NULL: read the header the compressor wrote. window_bits caps
         * the buffer; the header selects the actual window. */
        if (tamp_decompressor_init(&d, NULL, dwin, MAX_WINDOW_BITS) != TAMP_OK) {
            printf("FAIL: decompressor_init window=%u\n", window);
            return 1;
        }
        size_t total_out = 0, in_pos = 0;
        for (;;) {
            size_t dw = 0, di = 0;
            tamp_res dr = tamp_decompressor_decompress(&d, deco, ochunk, &dw, comp + in_pos, cw - in_pos, &di);
            if (dr < 0 && dr != TAMP_INPUT_EXHAUSTED) {
                printf("FAIL: decompress err=%d window=%u n=%zu ochunk=%zu\n", dr, window, n, ochunk);
                return 1;
            }
            if (total_out + dw > sizeof(full)) {
                printf("FAIL: output overflow window=%u n=%zu ochunk=%zu\n", window, n, ochunk);
                return 1;
            }
            memcpy(full + total_out, deco, dw);
            total_out += dw;
            in_pos += di;
            if (dr == TAMP_INPUT_EXHAUSTED && di == 0 && dw == 0) break;
            if (in_pos >= cw && dw == 0) break;
        }
        if (total_out != n || memcmp(full, data, n) != 0) {
            size_t i = 0;
            while (i < n && i < total_out && full[i] == data[i]) i++;
            printf("FAIL: MISMATCH window=%u n=%zu ochunk=%zu total_out=%zu first_diff=%zu got=%d want=%d\n", window, n,
                   ochunk, total_out, i, i < total_out ? full[i] : -1, i < n ? data[i] : -1);
            return 1;
        }
    }
    return 0;
}

/* --- OOB-writeback regression (see decompressor.c: both reservoir bodies must
 * TAMP_RES_WRITEBACK() before TAMP_DECOMP_RETURN(TAMP_OOB), so input_consumed
 * and bit_buffer_pos stay consistent with the offending token). Hand-crafted
 * classic streams (window=8, literal=8) whose N-th token references an
 * out-of-bounds window offset; every decode path (careful, non-reservoir fast
 * loop, reservoir fast loop, reservoir history mode) must report the same exact
 * token bit offset. --- */

/* MSB-first bit writer into a byte buffer (bits are emitted high-to-low). */
typedef struct {
    unsigned char *buf;
    size_t bitpos;
    size_t cap;
} bitwriter_t;
static void bw_put(bitwriter_t *w, uint32_t value, int nbits) {
    for (int i = nbits - 1; i >= 0; i--) {
        size_t byte = w->bitpos >> 3;
        int bit = 7 - (int)(w->bitpos & 7);
        if (byte < w->cap && ((value >> i) & 1u)) w->buf[byte] |= (unsigned char)(1u << bit);
        w->bitpos++;
    }
}

/* Decode a hand-crafted stream expected to fault OOB, and check that the decode
 * that returned it left input/bit_buffer consistent with the offending token.
 * `expected_bit_off` is the token's bit offset AFTER the 1-byte header, so
 * (input_consumed - 1) * 8 - bit_buffer_pos (the exact count of decoded bits)
 * must equal it. Returns 0 on pass. */
static int oob_case(const char *name, const unsigned char *in, size_t in_len, size_t expected_bit_off) {
    TampDecompressor d;
    if (tamp_decompressor_init(&d, NULL, dwin, MAX_WINDOW_BITS) != TAMP_OK) {
        printf("FAIL(%s): decompressor_init\n", name);
        return 1;
    }
    size_t written = 0, consumed = 0;
    tamp_res r = tamp_decompressor_decompress(&d, deco, sizeof(deco), &written, in, in_len, &consumed);
    if (r != TAMP_OOB) {
        printf("FAIL(%s): expected TAMP_OOB, got %d (consumed=%zu written=%zu)\n", name, r, consumed, written);
        return 1;
    }
    long long decoded_bits = (long long)(consumed - 1) * 8 - (long long)d.bit_buffer_pos;
    if (decoded_bits != (long long)expected_bit_off) {
        printf("FAIL(%s): OOB token bit offset %lld != expected %zu (consumed=%zu bit_buffer_pos=%u)\n", name,
               decoded_bits, expected_bit_off, consumed, d.bit_buffer_pos);
        return 1;
    }
    return 0;
}

/* Case 1: first token after the header is OOB (exercises the un-armed /
 * TAMP_RES_TOKEN_BODY body on reservoir builds). Header 0x18 = window 8,
 * literal 8, classic, 1-byte header. Token: pattern flag 0, huffman symbol 0
 * (match_size = min_pattern = 2), window offset 0xFF = 255 > 256 - 2 -> OOB.
 * Trailing filler so the fast loop's >=8-input precondition is met. */
static int oob_unarmed(void) {
    static unsigned char in[64];
    memset(in, 0, sizeof(in));
    bitwriter_t w = {in, 0, sizeof(in)};
    bw_put(&w, 0x18, 8); /* header */
    bw_put(&w, 0, 1);    /* pattern flag */
    bw_put(&w, 0, 1);    /* huffman symbol 0 -> match_size raw 0 */
    bw_put(&w, 0xFF, 8); /* window offset 255 (OOB) */
    return oob_case("oob_unarmed", in, sizeof(in), 0);
}

/* Case 2: 300 literals precede the OOB token so history mode arms (>= 256
 * output bytes) before it, exercising TAMP_RES_TOKEN_BODY_HISTORY's OOB path on
 * reservoir builds. Built with the bit writer, NOT compressor+FLUSH: a FLUSH
 * sets last_was_flush and blocks the next fast-loop entry, which would decode
 * the OOB token in the careful body and miss the armed path. Each literal is
 * 9 bits (flag 1 + 8 literal bits) on window 8 / literal 8. */
static int oob_armed(void) {
    static unsigned char in[1024];
    memset(in, 0, sizeof(in));
    bitwriter_t w = {in, 0, sizeof(in)};
    bw_put(&w, 0x18, 8); /* header */
    const size_t NLIT = 300;
    for (size_t i = 0; i < NLIT; i++) {
        bw_put(&w, 1, 1);                    /* literal flag */
        bw_put(&w, (uint32_t)(i & 0xFF), 8); /* 8 literal bits */
    }
    size_t tok_bit_off = NLIT * 9;                 /* OOB token's bit offset after the header */
    bw_put(&w, 0, 1);                              /* pattern flag */
    bw_put(&w, 0, 1);                              /* huffman symbol 0 -> match_size 2 */
    bw_put(&w, 0xFF, 8);                           /* window offset 255 (OOB) */
    for (int i = 0; i < 32; i++) bw_put(&w, 0, 8); /* filler: keep >=8 input at the token */
    size_t in_len = (w.bitpos + 7) / 8;
    return oob_case("oob_armed", in, in_len, tok_bit_off);
}

int main(void) {
    static unsigned char buf[MAX_INPUT];
    int fails = 0, tests = 0;
    const uint8_t windows[] = {8, 10, 12};

    for (size_t wi = 0; wi < sizeof(windows) / sizeof(windows[0]); wi++) {
        uint8_t window = windows[wi];
        size_t wsize = (size_t)1 << window;

        /* Periodic data: period swept across the window boundary so matches
         * repeatedly reference data straddling window_pos (seam-crossing). */
        for (size_t period = 1; period <= wsize + 8 && period <= 400; period += (period < 40 ? 1 : 11)) {
            size_t n = wsize * 4 + 37;
            if (n > sizeof(buf)) n = sizeof(buf);
            for (size_t i = 0; i < n; i++) buf[i] = (unsigned char)((i % period) * 7 + (i / period));
            tests++;
            fails += roundtrip(buf, n, window);
        }

        /* Structural variants: constant, ramp, coarse ramp, few-symbol (long
         * matches), ascii text, and fixed-seed random. */
        for (int variant = 0; variant < 6; variant++) {
            size_t n = wsize * 3 + 101;
            if (n > sizeof(buf)) n = sizeof(buf);
            for (size_t i = 0; i < n; i++) {
                switch (variant) {
                    case 0:
                        buf[i] = 0x5A;
                        break;
                    case 1:
                        buf[i] = (unsigned char)i;
                        break;
                    case 2:
                        buf[i] = (unsigned char)(i / 3);
                        break;
                    case 3:
                        buf[i] = (unsigned char)(xr() & 3);
                        break;
                    case 4:
                        buf[i] = (unsigned char)("the quick brown fox jumps over the lazy dog "[i % 44]);
                        break;
                    default:
                        buf[i] = (unsigned char)xr();
                        break;
                }
            }
            tests++;
            fails += roundtrip(buf, n, window);
        }
    }

    /* Fixed-seed random volume across all supported windows. */
    for (int t = 0; t < 200; t++) {
        size_t n = 200 + (xr() % (MAX_INPUT - 300));
        for (size_t i = 0; i < n; i++) buf[i] = (unsigned char)xr();
        uint8_t window = windows[xr() % 3];
        tests++;
        fails += roundtrip(buf, n, window);
    }

    /* OOB-writeback regression: both reservoir OOB returns must commit the
     * reservoir first so the reported input_consumed / bit_buffer_pos pin the
     * offending token exactly. Runs in every build config. */
    tests++;
    fails += oob_unarmed();
    tests++;
    fails += oob_armed();

    printf("%s: history_round_trip %d tests, %d failures\n", fails ? "FAIL" : "PASS", tests, fails);
    return fails ? 1 : 0;
}
