#include "decompressor.h"

#include "common.h"

/* Copy primitives for the hot paths. Platform-specific implementations live
 * with their platform component; TAMP_ESP32 builds already require the espidf
 * component sources on the include path (same contract as the extern
 * find_best_match in compressor.c). On generic targets the macros expand to
 * the plain byte loops / tamp_window_copy call they replaced (macros, not
 * inline functions: Cortex-M0+ codegen is sensitive to function boundaries). */
#if TAMP_ESP32
#include "private/tamp_copy.h"
#else

/* Copy count bytes from src to the output cursor and advance it. */
#if TAMP_FAST_OUTPUT_COPY
/* Word-at-a-time: window and output never overlap, and the copy never writes
 * past out+count, so this is safe whenever unaligned word access is cheap. */
#define TAMP_COPY_TO_OUTPUT(out, src, count)        \
    do {                                            \
        const unsigned char* _tamp_s = (src);       \
        uint8_t _tamp_n = (count);                  \
        while (_tamp_n >= 4) {                      \
            uint32_t _tamp_w;                       \
            __builtin_memcpy(&_tamp_w, _tamp_s, 4); \
            __builtin_memcpy((out), &_tamp_w, 4);   \
            (out) += 4;                             \
            _tamp_s += 4;                           \
            _tamp_n -= 4;                           \
        }                                           \
        while (_tamp_n--) {                         \
            *(out)++ = *_tamp_s++;                  \
        }                                           \
    } while (0)
#else
#define TAMP_COPY_TO_OUTPUT(out, src, count)                      \
    do {                                                          \
        for (uint8_t _tamp_i = 0; _tamp_i < (count); _tamp_i++) { \
            *(out)++ = (src)[_tamp_i];                            \
        }                                                         \
    } while (0)
#endif /* TAMP_FAST_OUTPUT_COPY */

#define TAMP_WINDOW_COPY(window, window_pos, window_offset, match_size, window_mask) \
    tamp_window_copy((window), (window_pos), (window_offset), (match_size), (window_mask))

#endif /* TAMP_ESP32 */

/* Update the circular window from a linear, non-overlapping snapshot of the
 * match bytes (the copy just written to output). Only DESTINATION wrap is
 * possible (match_size <= 134 << window_size, so at most one wrap); a snapshot
 * source cannot overlap the destination, so no reverse-copy path is needed.
 * window_pos_var is a plain uint16_t variable (not a pointer) to avoid spilling
 * it to the stack across a call. Guarded so a platform component may override. */
#ifndef TAMP_WINDOW_WRITE_FROM_OUTPUT

#if TAMP_FAST_WINDOW_COPY
/* Word-at-a-time linear segment copy. The while(>=4)+byte-tail structure never
 * writes past dst+n, so the second segment's start is never clobbered. */
#define TAMP_WINDOW_SEG_COPY_(dst, src, n)            \
    do {                                              \
        unsigned char* _tamp_sd = (dst);              \
        const unsigned char* _tamp_ss = (src);        \
        uint8_t _tamp_sn = (n);                       \
        while (_tamp_sn >= 4) {                       \
            uint32_t _tamp_sw;                        \
            __builtin_memcpy(&_tamp_sw, _tamp_ss, 4); \
            __builtin_memcpy(_tamp_sd, &_tamp_sw, 4); \
            _tamp_sd += 4;                            \
            _tamp_ss += 4;                            \
            _tamp_sn -= 4;                            \
        }                                             \
        while (_tamp_sn--) *_tamp_sd++ = *_tamp_ss++; \
    } while (0)
#else
#define TAMP_WINDOW_SEG_COPY_(dst, src, n)                                                                   \
    do {                                                                                                     \
        unsigned char* _tamp_sd = (dst);                                                                     \
        const unsigned char* _tamp_ss = (src);                                                               \
        uint8_t _tamp_sn = (n);                                                                              \
        for (uint8_t _tamp_si = 0; _tamp_si < _tamp_sn; _tamp_si++) _tamp_sd[_tamp_si] = _tamp_ss[_tamp_si]; \
    } while (0)
#endif /* TAMP_FAST_WINDOW_COPY */

#define TAMP_WINDOW_WRITE_FROM_OUTPUT(window, window_pos_var, src, match_size, window_size, window_mask) \
    do {                                                                                                 \
        const unsigned char* _tamp_wsrc = (src);                                                         \
        uint16_t _tamp_wpos = (window_pos_var);                                                          \
        uint16_t _tamp_wrem = (uint16_t)((window_size)-_tamp_wpos);                                      \
        uint8_t _tamp_wms = (match_size);                                                                \
        if (TAMP_LIKELY(_tamp_wms <= _tamp_wrem)) {                                                      \
            TAMP_WINDOW_SEG_COPY_((window) + _tamp_wpos, _tamp_wsrc, _tamp_wms);                         \
            (window_pos_var) = (uint16_t)((_tamp_wpos + _tamp_wms) & (window_mask));                     \
        } else {                                                                                         \
            uint8_t _tamp_wr = (uint8_t)_tamp_wrem;                                                      \
            TAMP_WINDOW_SEG_COPY_((window) + _tamp_wpos, _tamp_wsrc, _tamp_wr);                          \
            TAMP_WINDOW_SEG_COPY_((window), _tamp_wsrc + _tamp_wr, (uint8_t)(_tamp_wms - _tamp_wr));     \
            (window_pos_var) = (uint16_t)(_tamp_wms - _tamp_wr);                                         \
        }                                                                                                \
    } while (0)

#endif /* TAMP_WINDOW_WRITE_FROM_OUTPUT */

#if !TAMP_WINDOW_FROM_OUTPUT && !TAMP_ESP32
/* Out-of-line linear window update from the just-written output snapshot, for
 * portable builds that keep tamp_window_copy on the resume path. The snapshot
 * source never overlaps the window destination, so no reverse-copy overlap
 * check and no per-byte source masking are needed. NOINLINE is mandatory: at
 * -O3 GCC inlines this regardless of cost heuristics, and the inlined update
 * regresses Cortex-M0+ (register pressure) - the whole point is to keep it a
 * call. n and mask are packed into one word to stay within 4 register args
 * (Cortex-M0+). Returns the new window_pos. */
static uint16_t TAMP_NOINLINE tamp_window_write_from_output_fn(unsigned char* window, const unsigned char* src,
                                                               uint16_t pos, uint32_t n_and_mask) {
    uint16_t mask = (uint16_t)n_and_mask;
    uint8_t n = (uint8_t)(n_and_mask >> 16);
    uint16_t rem = (uint16_t)((mask + 1) - pos);
    if (TAMP_LIKELY(n <= rem)) {
        for (uint8_t i = 0; i < n; i++) window[pos + i] = src[i];
        return (uint16_t)((pos + n) & mask);
    }
    uint8_t r = (uint8_t)rem;
    for (uint8_t i = 0; i < r; i++) window[pos + i] = src[i];
    uint8_t tail = (uint8_t)(n - r);
    for (uint8_t i = 0; i < tail; i++) window[i] = src[r + i];
    return tail;
}
#endif

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define FLUSH 14

/* Compile-time-pinned stream configuration (opt-in; see common.h).
 * TAMP_FIXED_WINDOW_BITS / TAMP_FIXED_LITERAL_BITS let a build that only ever
 * decodes one configuration fold every window/literal shift to an immediate;
 * with both pinned, min_pattern_size is a compile-time constant too. The hot
 * path reads the configuration exclusively through the locals initialized with
 * these macros, so folding the initializer folds all downstream uses. Undefined
 * => the runtime value from the decompressor state (current behavior).
 * tamp_decompressor_populate_from_conf rejects any stream whose header disagrees
 * with a pinned value, so these constants can be trusted unconditionally. */
#ifdef TAMP_FIXED_WINDOW_BITS
#define TAMP_CONF_WINDOW_INIT(d) TAMP_FIXED_WINDOW_BITS
#else
#define TAMP_CONF_WINDOW_INIT(d) ((d)->conf_window)
#endif
#ifdef TAMP_FIXED_LITERAL_BITS
#define TAMP_CONF_LITERAL_INIT(d) TAMP_FIXED_LITERAL_BITS
#else
#define TAMP_CONF_LITERAL_INIT(d) ((d)->conf_literal)
#endif
#if defined(TAMP_FIXED_WINDOW_BITS) && defined(TAMP_FIXED_LITERAL_BITS)
/* Mirrors tamp_compute_min_pattern_size(window, literal). */
#define TAMP_CONF_MIN_PATTERN_INIT(d) (2 + (TAMP_FIXED_WINDOW_BITS > (10 + ((TAMP_FIXED_LITERAL_BITS - 5) << 1))))
#else
#define TAMP_CONF_MIN_PATTERN_INIT(d) ((d)->min_pattern_size)
#endif

#if TAMP_EXTENDED_DECOMPRESS
/* Token state for extended decode suspend/resume (2 bits).
 * TOKEN_RLE and TOKEN_EXT_MATCH_FRESH are arranged so that:
 *     token_state = match_size - (TAMP_RLE_SYMBOL - 1)
 * maps TAMP_RLE_SYMBOL (12) -> 1 and TAMP_EXTENDED_MATCH_SYMBOL (13) -> 2.
 */
#define TOKEN_NONE 0
#define TOKEN_RLE 1
#define TOKEN_EXT_MATCH_FRESH 2
#define TOKEN_EXT_MATCH 3 /* Resume: have match_size, need window_offset */
#endif

/* token_state only exists in the struct when extended decode is compiled in;
 * classic-only builds have no extended tokens, so pending state is always 0. */
#if TAMP_EXTENDED_DECOMPRESS
#define TAMP_PENDING_TOKEN_STATE(d) ((d)->token_state)
#else
#define TAMP_PENDING_TOKEN_STATE(d) 0
#endif

/**
 * Huffman lookup table indexed by 7 bits (after first "1" bit consumed).
 * Upper 4 bits = additional bits to consume, lower 4 bits = symbol (14 = FLUSH).
 *
 * Note: A 64-byte table with special-cased symbol 1 was tried but was ~10% slower
 * and only saved 8 bytes in final firmware due to added branch logic.
 */
TAMP_STATIC_CONST uint8_t HUFFMAN_TABLE[128] = {
    50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50,  50,  85,  85,  85, 85, 122, 123, 104, 104, 86, 86,
    86, 86, 93, 93, 93, 93, 68, 68, 68, 68, 68, 68, 68, 68, 105, 105, 124, 126, 87, 87, 87,  87,  51,  51,  51, 51,
    51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 51, 17, 17, 17,  17,  17,  17,  17, 17, 17,  17,  17,  17,  17, 17,
    17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,  17,  17,  17,  17, 17, 17,  17,  17,  17,  17, 17,
    17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,  17,  17,  17,  17, 17, 17,  17,  17,  17};

/**
 * @brief Decode huffman symbol + optional trailing bits from bit buffer.
 *
 * Modifies bit_buffer and bit_buffer_pos in place. Caller is responsible
 * for committing to decompressor state if needed.
 *
 * @param bit_buffer Pointer to bit buffer (modified in place)
 * @param bit_buffer_pos Pointer to bit position (modified in place)
 * @param trailing_bits Number of trailing bits to read (0, 3, or 4)
 * @param result Output: (huffman << trailing_bits) + trailing (max 239 for trailing_bits=4)
 * @return TAMP_OK on success, TAMP_INPUT_EXHAUSTED if more bits needed
 */
static tamp_res decode_huffman(uint32_t* bit_buffer, uint8_t* bit_buffer_pos, uint8_t trailing_bits, uint8_t* result) {
    /* Need at least 1 bit for huffman, plus trailing bits */
    if (TAMP_UNLIKELY(*bit_buffer_pos < 1 + trailing_bits)) return TAMP_INPUT_EXHAUSTED;

    /* Decode huffman symbol */
    int8_t huffman_value;
    (*bit_buffer_pos)--;
    if (TAMP_LIKELY((*bit_buffer >> 31) == 0)) {
        /* Symbol 0: code "0" */
        *bit_buffer <<= 1;
        huffman_value = 0;
    } else {
        /* All other symbols: use 128-entry table indexed by next 7 bits */
        *bit_buffer <<= 1;
        uint8_t code = HUFFMAN_TABLE[*bit_buffer >> (32 - 7)];
        uint8_t bit_len = code >> 4;
        if (TAMP_UNLIKELY(*bit_buffer_pos < bit_len + trailing_bits)) return TAMP_INPUT_EXHAUSTED;
        *bit_buffer <<= bit_len;
        *bit_buffer_pos -= bit_len;
        huffman_value = code & 0xF;
    }

    /* Read trailing bits (skip if trailing_bits==0 to avoid undefined shift) */
    if (trailing_bits) {
        uint8_t trailing = *bit_buffer >> (32 - trailing_bits);
        *bit_buffer <<= trailing_bits;
        *bit_buffer_pos -= trailing_bits;
        *result = (huffman_value << trailing_bits) + trailing;
    } else {
        *result = huffman_value;
    }

    return TAMP_OK;
}

#if TAMP_EXTENDED_DECOMPRESS

/**
 * @brief Decode RLE token and write repeated bytes to output.
 *
 * RLE format: huffman(count_high) + trailing_bits(count_low)
 * rle_count = (count_high << 4) + count_low + 2
 */
static tamp_res decode_rle(TampDecompressor* d, unsigned char** output, const unsigned char* output_end) {
    uint8_t rle_count; /* max 241: (14 << 4) + 15 + 2 */
    uint8_t skip = d->skip_bytes;

    if (skip > 0) {
        /* Resume from output-full: rle_count saved in pending_window_offset */
        rle_count = d->pending_window_offset;
    } else {
        /* Fresh decode */
        uint32_t bit_buffer = d->bit_buffer;
        uint8_t bit_buffer_pos = d->bit_buffer_pos;
        uint8_t raw;
        tamp_res res = decode_huffman(&bit_buffer, &bit_buffer_pos, TAMP_LEADING_RLE_BITS, &raw);
        if (res != TAMP_OK) return res;
        d->bit_buffer = bit_buffer;
        d->bit_buffer_pos = bit_buffer_pos;
        rle_count = raw + 2;
    }

    /* Get the byte to repeat (last written byte) */
    uint16_t prev_pos = (d->window_pos - 1) & ((1u << TAMP_CONF_WINDOW_INIT(d)) - 1);
    uint8_t symbol = d->window[prev_pos];

    /* Calculate how many to write this call */
    uint8_t remaining_count = rle_count - skip;
    size_t output_space = output_end - *output;
    uint8_t to_write;

    if (TAMP_UNLIKELY(remaining_count > output_space)) {
        /* Partial write - save state for resume */
        to_write = output_space;
        d->skip_bytes = skip + to_write;
        d->token_state = TOKEN_RLE;
        d->pending_window_offset = rle_count;
    } else {
        /* Complete write */
        to_write = remaining_count;
        d->skip_bytes = 0;
        d->token_state = TOKEN_NONE;
    }

    /* Write repeated bytes to output */
    TAMP_MEMSET(*output, symbol, to_write);
    *output += to_write;

    /* Update window only on first chunk (skip==0).
     * Write up to TAMP_RLE_MAX_WINDOW or until end of buffer (no wrap). */
    if (skip == 0) {
        const uint16_t window_size = 1u << TAMP_CONF_WINDOW_INIT(d);
        uint16_t remaining = window_size - d->window_pos;
        uint8_t window_write = MIN(MIN(rle_count, TAMP_RLE_MAX_WINDOW), remaining); /* max 8 */
        for (uint8_t i = 0; i < window_write; i++) {
            d->window[d->window_pos++] = symbol;
        }
        d->window_pos &= (window_size - 1);
    }

    return (d->token_state == TOKEN_NONE) ? TAMP_OK : TAMP_OUTPUT_FULL;
}

/**
 * @brief Decode extended match token and copy from window to output.
 *
 * NEW FORMAT: huffman(size_high) + trailing_bits(size_low) + window_offset
 * match_size = (size_high << 3) + size_low + min_pattern_size + 12
 *
 * State machine:
 * - Fresh: decode huffman+trailing, then window_offset
 * - TOKEN_EXT_MATCH: have match_size, need window_offset
 * - Output-full resume (skip > 0): have both match_size and window_offset
 */
static tamp_res decode_extended_match(TampDecompressor* d, unsigned char** output, const unsigned char* output_end) {
    const uint8_t conf_window = TAMP_CONF_WINDOW_INIT(d);
    uint16_t window_offset;
    uint8_t match_size; /* max 134: (14<<3)+7 + 3 + 12 */
    uint8_t skip = d->skip_bytes;

    if (skip > 0) {
        /* Resume from output-full: both values saved */
        window_offset = d->pending_window_offset;
        match_size = d->pending_match_size;
    } else if (d->token_state == TOKEN_EXT_MATCH) {
        /* Resume: have match_size, need window_offset */
        match_size = d->pending_match_size;

        if (TAMP_UNLIKELY(d->bit_buffer_pos < conf_window)) return TAMP_INPUT_EXHAUSTED;
        window_offset = d->bit_buffer >> (32 - conf_window);
        d->bit_buffer <<= conf_window;
        d->bit_buffer_pos -= conf_window;
    } else {
        /* Fresh decode: huffman+trailing first, then window_offset */
        uint32_t bit_buffer = d->bit_buffer;
        uint8_t bit_buffer_pos = d->bit_buffer_pos;
        uint8_t raw;
        tamp_res res = decode_huffman(&bit_buffer, &bit_buffer_pos, TAMP_LEADING_EXTENDED_MATCH_BITS, &raw);
        if (res != TAMP_OK) return res;
        match_size = raw + TAMP_CONF_MIN_PATTERN_INIT(d) + 12;

        /* Now decode window_offset */
        if (TAMP_UNLIKELY(bit_buffer_pos < conf_window)) {
            /* Save match_size and return */
            d->bit_buffer = bit_buffer;
            d->bit_buffer_pos = bit_buffer_pos;
            d->token_state = TOKEN_EXT_MATCH;
            d->pending_match_size = match_size;
            return TAMP_INPUT_EXHAUSTED;
        }
        window_offset = bit_buffer >> (32 - conf_window);
        bit_buffer <<= conf_window;
        bit_buffer_pos -= conf_window;
        d->bit_buffer = bit_buffer;
        d->bit_buffer_pos = bit_buffer_pos;
    }

    /* Security check: validate window bounds */
    const uint32_t window_size = (1u << conf_window);
    if (TAMP_UNLIKELY((uint32_t)window_offset >= window_size ||
                      (uint32_t)window_offset + (uint32_t)match_size > window_size)) {
        return TAMP_OOB;
    }

    /* Calculate how many to write this call */
    uint8_t remaining_count = match_size - skip;
    size_t output_space = output_end - *output;
    uint8_t to_write;

    if (TAMP_UNLIKELY(remaining_count > output_space)) {
        /* Partial write - save state for resume */
        to_write = output_space;
        d->skip_bytes = skip + output_space;
        d->token_state = TOKEN_EXT_MATCH; /* Reuse for output-full */
        d->pending_window_offset = window_offset;
        d->pending_match_size = match_size;
    } else {
        /* Complete write */
        to_write = remaining_count;
        d->skip_bytes = 0;
        d->token_state = TOKEN_NONE;
    }

    /* Copy from window to output */
    uint16_t src_offset = window_offset + skip;
    TAMP_COPY_TO_OUTPUT(*output, d->window + src_offset, to_write);

    /* Update window only on complete decode.
     * Write up to end of buffer (no wrap), matching RLE behavior. */
    if (d->token_state == TOKEN_NONE) {
        uint16_t wp = d->window_pos;
        uint16_t remaining = window_size - wp;
        uint8_t window_write = (match_size < remaining) ? match_size : remaining;
#if TAMP_WINDOW_FROM_OUTPUT
        if (skip == 0) {
            /* Single-call complete: output's last match_size bytes are the full
             * match; window_write <= remaining so this never wraps. */
            TAMP_WINDOW_WRITE_FROM_OUTPUT(d->window, wp, *output - match_size, window_write, window_size,
                                          window_size - 1);
        } else {
            /* Resume completion: the match start is not in this output buffer. */
            TAMP_WINDOW_COPY(d->window, &wp, window_offset, window_write, window_size - 1);
        }
#elif !TAMP_ESP32
        if (skip == 0) {
            wp = tamp_window_write_from_output_fn(d->window, *output - match_size, wp,
                                                  ((uint32_t)window_write << 16) | (window_size - 1));
        } else {
            TAMP_WINDOW_COPY(d->window, &wp, window_offset, window_write, window_size - 1);
        }
#else
        TAMP_WINDOW_COPY(d->window, &wp, window_offset, window_write, window_size - 1);
#endif
        d->window_pos = wp;
    }

    return (d->token_state == TOKEN_NONE) ? TAMP_OK : TAMP_OUTPUT_FULL;
}
#endif /* TAMP_EXTENDED_DECOMPRESS */

tamp_res tamp_decompressor_read_header(TampConf* conf, const unsigned char* input, size_t input_size,
                                       size_t* input_consumed_size) {
    if (input_consumed_size) (*input_consumed_size) = 0;
    if (input_size == 0) return TAMP_INPUT_EXHAUSTED;

    // Validate all header bytes before mutating conf.
    size_t header_size = 1 + (input[0] & 0x1);
    if (input_size < header_size) return TAMP_INPUT_EXHAUSTED;
    // All bits in byte 2 are reserved for future use; reject if any are set.
    if (header_size >= 2 && input[1]) return TAMP_INVALID_CONF;

    conf->window = ((input[0] >> 5) & 0x7) + 8;
    conf->literal = ((input[0] >> 3) & 0x3) + 5;
    conf->use_custom_dictionary = ((input[0] >> 2) & 0x1);
    conf->extended = ((input[0] >> 1) & 0x1);
    // more_header (byte 1 bit 0) implies dictionary_reset.
    conf->dictionary_reset = input[0] & 0x1;

    if (input_consumed_size) (*input_consumed_size) += header_size;

    return TAMP_OK;
}

/**
 * Populate the rest of the decompressor structure after the following fields have been populated:
 *   * window
 *   * window_bits_max
 */
static TAMP_OPTIMIZE_SIZE tamp_res tamp_decompressor_populate_from_conf(TampDecompressor* decompressor,
                                                                        uint8_t conf_window, uint8_t conf_literal,
                                                                        uint8_t conf_use_custom_dictionary,
                                                                        uint8_t conf_extended,
                                                                        uint8_t conf_dictionary_reset) {
    if (conf_window < 8 || conf_window > 15) return TAMP_INVALID_CONF;
    if (conf_literal < 5 || conf_literal > 8) return TAMP_INVALID_CONF;
    if (conf_window > decompressor->window_bits_max) return TAMP_INVALID_CONF;
#ifdef TAMP_FIXED_WINDOW_BITS
    // Build pinned to a single window configuration: reject any other stream up
    // front so the hot path can treat the window bits as a compile-time constant.
    if (conf_window != TAMP_FIXED_WINDOW_BITS) return TAMP_INVALID_CONF;
#endif
#ifdef TAMP_FIXED_LITERAL_BITS
    if (conf_literal != TAMP_FIXED_LITERAL_BITS) return TAMP_INVALID_CONF;
#endif
#if !TAMP_EXTENDED_DECOMPRESS
    // Reject before committing any state: marking the decompressor configured and
    // then returning an error would let a retrying caller decode the extended
    // stream as classic format.
    if (conf_extended) return TAMP_INVALID_CONF;  // Extended stream but extended support not compiled in
#endif
    if (!conf_use_custom_dictionary)
        tamp_initialize_dictionary(decompressor->window, (size_t)1 << conf_window, conf_extended ? conf_literal : 8);

    decompressor->conf_window = conf_window;
    decompressor->conf_literal = conf_literal;
    decompressor->min_pattern_size = tamp_compute_min_pattern_size(conf_window, conf_literal);
    decompressor->configured = true;
    decompressor->conf_extended = conf_extended;
    decompressor->conf_dictionary_reset = conf_dictionary_reset;

    return TAMP_OK;
}

tamp_res tamp_decompressor_init(TampDecompressor* decompressor, const TampConf* conf, unsigned char* window,
                                uint8_t window_bits) {
    tamp_res res = TAMP_OK;

    // Validate window_bits parameter
    if (window_bits < 8 || window_bits > 15) return TAMP_INVALID_CONF;

    TAMP_MEMSET(decompressor, 0, sizeof(TampDecompressor));
    decompressor->window = window;
    decompressor->window_bits_max = window_bits;
    if (conf) {
        res = tamp_decompressor_populate_from_conf(decompressor, conf->window, conf->literal,
                                                   conf->use_custom_dictionary, conf->extended, conf->dictionary_reset);
    }

    return res;
}

/**
 * @brief Refill bit buffer from input stream.
 *
 * Consumes bytes from input until bit_buffer has at least 25 bits or input is exhausted.
 *
 * Two variants, selected by TAMP_FAST_BIT_REFILL (see common.h): the fast
 * variant works on locals with a single writeback, since the unsigned char
 * loads may alias anything and force a reload/store per byte otherwise.
 *
 * NOTE: NOINLINE saves ~192 bytes on armv6m but causes ~10% decompression
 * speed regression. Keep this inlined for performance.
 */
#if TAMP_FAST_BIT_REFILL
static inline void refill_bit_buffer(TampDecompressor* d, const unsigned char** input, const unsigned char* input_end) {
    const unsigned char* in = *input;
    uint32_t bit_buffer = d->bit_buffer;
    uint8_t bit_buffer_pos = d->bit_buffer_pos;
    while (in != input_end && bit_buffer_pos <= 24) {
        bit_buffer_pos += 8;
        bit_buffer |= (uint32_t)*in++ << (32 - bit_buffer_pos);
    }
    d->bit_buffer = bit_buffer;
    d->bit_buffer_pos = bit_buffer_pos;
    *input = in;
}
#else
static inline void refill_bit_buffer(TampDecompressor* d, const unsigned char** input, const unsigned char* input_end) {
    while (*input != input_end && d->bit_buffer_pos <= 24) {
        d->bit_buffer_pos += 8;
        d->bit_buffer |= (uint32_t) * (*input) << (32 - d->bit_buffer_pos);
        (*input)++;
    }
}
#endif /* TAMP_FAST_BIT_REFILL */

#if TAMP_FAST_DECODE_LOOP
/* Window update for the fast-loop token body, selected by the same platform
 * flags that pick the careful path's window-update strategy. */
#if TAMP_WINDOW_FROM_OUTPUT
#define TAMP_FAST_WINDOW_UPDATE_(wp, ms, wsz) \
    TAMP_WINDOW_WRITE_FROM_OUTPUT(decompressor->window, (wp), output - (ms), (ms), (wsz), window_mask)
#elif !TAMP_ESP32
#define TAMP_FAST_WINDOW_UPDATE_(wp, ms, wsz)                                          \
    (wp) = tamp_window_write_from_output_fn(decompressor->window, output - (ms), (wp), \
                                            ((uint32_t)(uint8_t)(ms) << 16) | window_mask)
#else
#define TAMP_FAST_WINDOW_UPDATE_(wp, ms, wsz) \
    TAMP_WINDOW_COPY(decompressor->window, &(wp), window_offset, (ms), window_mask)
#endif

/* The fast loop decodes from a 64-bit bit reservoir (see common.h,
 * TAMP_FAST_DECODE_LOOP, for the measured numbers). `bb` holds valid bits in
 * its top `rbits` bits; a decode reads the top 32 (`bb >> 32`). A single
 * unconditional 4-byte refill fires only when <=32 valid bits remain (~every
 * 1-2 tokens), instead of a per-token refill of the 32-bit struct bit_buffer.
 * bb's bits below the top `rbits` are always zero
 * inside the loop (consumption `bb <<=` shifts zeros in at the bottom; each
 * refill ORs a chunk into exactly that zero region, contiguous with the live
 * bits), which the writeback mask relies on.
 *
 * Writeback discipline (runs on EVERY loop exit before the struct bit_buffer is
 * read again). The invariant rbits == 8*bytes_read - bits_consumed holds across
 * the pushback, so the next input byte always begins exactly where the
 * reservoir's valid bits end - byte alignment is automatic, no matter what
 * bit_buffer_pos the loop was entered with:
 *   1. while (rbits > 32) { input -= 1; rbits -= 8; }
 *        Un-read the surplus whole bytes (rbits now in [25,32] when the loop
 *        ran a token, else the untouched entry value). Only decrements the
 *        accounting + input cursor; bb is untouched, so the pushed-back bits
 *        remain sitting below the new top-`rbits` region.
 *   2. bit_buffer = (top 32 bits of bb) AND a mask that clears every bit below
 *        the top `rbits` - those hold the just-un-read data, and the next
 *        refill_bit_buffer ORs new bytes into exactly that region, so they must
 *        be zero for the OR to stay clean.
 *   3. bit_buffer_pos = rbits.
 * The uncommitted-token contract is: on any structural/seam break, bb/rbits are
 * left describing the exact bitstream position the careful body / decode_*
 * helpers must resume from, and after the writeback the struct bit_buffer is a
 * valid 32-bit-style buffer (top bit_buffer_pos bits live, low bits zero). */

/* Refill 4 whole bytes when the reservoir has room. Callers guarantee >=4 input
 * bytes remain via the loop precondition (>=8 input covers the two per-iteration
 * refills). Reads bytes MSB-first, same order as refill_bit_buffer. */
#define TAMP_RES_REFILL()                                                                                           \
    do {                                                                                                            \
        if (rbits <= 32) {                                                                                          \
            uint32_t _chunk = ((uint32_t)input[0] << 24) | ((uint32_t)input[1] << 16) | ((uint32_t)input[2] << 8) | \
                              (uint32_t)input[3];                                                                   \
            input += 4;                                                                                             \
            bb |= (uint64_t)_chunk << (32 - rbits);                                                                 \
            rbits += 32;                                                                                            \
        }                                                                                                           \
    } while (0)

/* Commit the reservoir back to the 32-bit struct bit_buffer (see discipline),
 * then restore the fast-loop's >=25-bit invariant with a checked refill.
 *
 * The refill is REQUIRED, not an optimization: the loop can exit (output
 * precondition) with rbits as low as ~9, so bit_buffer_pos would be < 25. The
 * careful body runs immediately after a precondition exit with NO intervening
 * refill and needs up to 24 bits for one classic token, so it would return
 * TAMP_INPUT_EXHAUSTED prematurely (mid-buffer) without this top-up. */
#define TAMP_RES_WRITEBACK()                                \
    do {                                                    \
        while (rbits > 32) {                                \
            input -= 1;                                     \
            rbits -= 8;                                     \
        }                                                   \
        uint32_t _hi = (uint32_t)(bb >> 32);                \
        _hi &= (uint32_t)(0xFFFFFFFFu << (32 - rbits));     \
        decompressor->bit_buffer = _hi;                     \
        decompressor->bit_buffer_pos = (uint8_t)rbits;      \
        refill_bit_buffer(decompressor, &input, input_end); \
    } while (0)

/* Extended-symbol break for the reservoir plain body: consume only the
 * is_literal + type-huffman bits (decode_rle/decode_extended_match re-decode
 * the count/window-offset from the committed bit_buffer), set token_state,
 * break. Empty in classic-only builds. */
#if TAMP_EXTENDED_DECOMPRESS
#define TAMP_RES_EXT_BREAK_(cons, ms)                                        \
    if (TAMP_UNLIKELY(extended_enabled && (ms) >= TAMP_RLE_SYMBOL)) {        \
        bb <<= (cons);                                                       \
        rbits -= (int32_t)(cons);                                            \
        decompressor->token_state = (uint8_t)((ms) - (TAMP_RLE_SYMBOL - 1)); \
        break;                                                               \
    }
#else
#define TAMP_RES_EXT_BREAK_(cons, ms)
#endif

/* Fast-loop classic-token body (writes the ring), decoding from bb/rbits.
 * FLUSH aligns to the next stream byte boundary the same way
 * `bit_buffer_pos & ~7` does: the padding to skip equals post & 7 where
 * post = rbits - consumed (post ~= -consumed_total mod 8). */
#define TAMP_RES_TOKEN_BODY()                                                                  \
    {                                                                                          \
        uint32_t _top = (uint32_t)(bb >> 32);                                                  \
        if (TAMP_UNLIKELY(_top >> 31)) {                                                       \
            /* Literal. */                                                                     \
            uint32_t _t = _top << 1;                                                           \
            uint8_t literal = _t >> (32 - conf_literal);                                       \
            bb <<= (1 + conf_literal);                                                         \
            rbits -= (int32_t)(1 + conf_literal);                                              \
            uint16_t wp = decompressor->window_pos;                                            \
            decompressor->window_pos = (wp + 1) & window_mask;                                 \
            decompressor->window[wp] = literal;                                                \
            *output++ = literal;                                                               \
        } else {                                                                               \
            uint32_t _t = _top << 1; /* shift out the is_literal flag */                       \
            uint32_t consumed = 1;                                                             \
            uint8_t match_size;                                                                \
            if (TAMP_LIKELY((_t >> 31) == 0)) {                                                \
                _t <<= 1;                                                                      \
                consumed += 1;                                                                 \
                match_size = 0;                                                                \
            } else {                                                                           \
                _t <<= 1;                                                                      \
                uint8_t code = HUFFMAN_TABLE[_t >> (32 - 7)];                                  \
                uint8_t bit_len = code >> 4;                                                   \
                _t <<= bit_len;                                                                \
                consumed += 1 + bit_len;                                                       \
                match_size = code & 0xF;                                                       \
            }                                                                                  \
            if (TAMP_UNLIKELY(match_size == FLUSH)) {                                          \
                int32_t post = rbits - (int32_t)consumed;                                      \
                uint32_t align = (uint32_t)(post & 7);                                         \
                bb <<= (consumed + align);                                                     \
                rbits = post - (int32_t)align;                                                 \
                decompressor->last_was_flush = 1;                                              \
                break;                                                                         \
            }                                                                                  \
            TAMP_RES_EXT_BREAK_(consumed, match_size)                                          \
            match_size += min_pattern_size;                                                    \
            uint16_t window_offset = _t >> (32 - conf_window);                                 \
            const uint32_t window_size = (1u << conf_window);                                  \
            if (TAMP_UNLIKELY((uint32_t)window_offset > window_size - (uint32_t)match_size)) { \
                /* Commit the reservoir before returning: the OOB token is left                \
                 * uncommitted (bb/rbits not advanced past it), so the writeback               \
                 * pushes back the read-ahead surplus and leaves bit_buffer /                  \
                 * bit_buffer_pos / input consistent with the offending token. */              \
                TAMP_RES_WRITEBACK();                                                          \
                TAMP_DECOMP_RETURN(TAMP_OOB);                                                  \
            }                                                                                  \
            bb <<= (consumed + conf_window);                                                   \
            rbits -= (int32_t)(consumed + conf_window);                                        \
            TAMP_COPY_TO_OUTPUT(output, decompressor->window + window_offset, match_size);     \
            uint16_t wp = decompressor->window_pos;                                            \
            TAMP_FAST_WINDOW_UPDATE_(wp, match_size, window_size);                             \
            decompressor->window_pos = wp;                                                     \
        }                                                                                      \
    }

/* The hot fast loop, extracted from tamp_decompressor_decompress_cb so the
 * surrounding careful body (cold in fast-loop builds: buffer tails, resume
 * state, extended dispatch glue) can be compiled -Os while this stays -O3.
 * Called once per outer-loop iteration, so the call overhead and the per-call
 * conf reloads are amortized over every token the loop consumes. Cursors are
 * passed by reference and written back on every exit; returns TAMP_OK on a
 * normal exit (precondition failure or structural break - the caller re-checks
 * pending/flush state) or TAMP_OOB on a malicious offset.
 *
 * Without TAMP_COMPACT_CAREFUL_BODY the whole function is inlined back into
 * the -O3 careful body: keeping it outlined there costs flash (+304 B on M0+)
 * for duplicated conf loads and call glue with no Os payoff. */
#if TAMP_COMPACT_CAREFUL_BODY
static TAMP_NOINLINE tamp_res
#else
static TAMP_ALWAYS_INLINE tamp_res
#endif
tamp_fast_decode_loop(TampDecompressor* decompressor, const unsigned char** input_p, const unsigned char* input_end,
                      unsigned char** output_p, const unsigned char* output_end) {
    const unsigned char* input = *input_p;
    unsigned char* output = *output_p;
    const uint8_t conf_window = TAMP_CONF_WINDOW_INIT(decompressor);
    const uint8_t conf_literal = TAMP_CONF_LITERAL_INIT(decompressor);
    const uint8_t min_pattern_size = TAMP_CONF_MIN_PATTERN_INIT(decompressor);
    const uint16_t window_mask = (1 << conf_window) - 1;
#if TAMP_EXTENDED_DECOMPRESS
    const bool extended_enabled = decompressor->conf_extended;
#endif
    tamp_res res = TAMP_OK;
/* Cursor writeback + return for the token-body macros' error paths; the
 * caller's TAMP_DECOMP_RETURN derives the *_size accounting from the
 * written-back cursors. */
#define TAMP_DECOMP_RETURN(code) \
    do {                         \
        res = (code);            \
        goto tamp_fast_done;     \
    } while (0)
    /* Single token per iteration: the reservoir already amortizes the
     * refill to ~one per 1-2 tokens, and a two-token unroll here
     * measured SLOWER on real M7 hardware (H7B0: -3.1% throughput)
     * while costing ~1 KB of flash. (QEMU insn counts disagree on M4:
     * unroll -4% insns there; hardware-unverified.) */
    uint64_t bb = (uint64_t)decompressor->bit_buffer << 32;
    int32_t rbits = decompressor->bit_buffer_pos;
    while ((size_t)(input_end - input) >= 4 && (size_t)(output_end - output) >= 32) {
        TAMP_RES_REFILL();
        TAMP_RES_TOKEN_BODY()
    }
    TAMP_RES_WRITEBACK();
#undef TAMP_DECOMP_RETURN
tamp_fast_done:
    *input_p = input;
    *output_p = output;
    return res;
}

#endif /* TAMP_FAST_DECODE_LOOP */

#if TAMP_HAS_GCC_OPTIMIZE
#pragma GCC push_options
#pragma GCC optimize("-fno-tree-pre")
#endif
/* With TAMP_COMPACT_CAREFUL_BODY every hot token goes through
 * tamp_fast_decode_loop above and the remaining body (header parsing,
 * resume/tail tokens, extended dispatch glue) is compiled for size. Without
 * it the careful body keeps -O3: on portable builds it IS the hot loop. */
#if TAMP_COMPACT_CAREFUL_BODY
#define TAMP_DECOMPRESS_CB_OPT TAMP_OPTIMIZE_SIZE
#else
#define TAMP_DECOMPRESS_CB_OPT
#endif
TAMP_DECOMPRESS_CB_OPT
tamp_res tamp_decompressor_decompress_cb(TampDecompressor* decompressor, unsigned char* output, size_t output_size,
                                         size_t* output_written_size, const unsigned char* input, size_t input_size,
                                         size_t* input_consumed_size, tamp_callback_t callback, void* user_data) {
    size_t input_consumed_size_proxy;
    size_t output_written_size_proxy;
    tamp_res res;
    const unsigned char* input_end = input + input_size;
    const unsigned char* output_end = output + output_size;

    if (!output_written_size) output_written_size = &output_written_size_proxy;
    if (!input_consumed_size) input_consumed_size = &input_consumed_size_proxy;

    *input_consumed_size = 0;
    *output_written_size = 0;

/* Every exit derives input/output progress from the cursors. The hot loop
 * never touches *output_written_size / *input_consumed_size: a per-token RMW
 * through a size_t pointer may alias the decompressor fields (size_t ==
 * uint32_t on 32-bit targets), costing a load/store per iteration plus forced
 * field reloads. *input_consumed_size already holds the header bytes counted
 * before input_start was snapshotted. */
#define TAMP_DECOMP_RETURN(code)                                \
    do {                                                        \
        *output_written_size = (size_t)(output - output_start); \
        *input_consumed_size += (size_t)(input - input_start);  \
        return (code);                                          \
    } while (0)
    const unsigned char* const output_start = output;

    if (TAMP_UNLIKELY(!decompressor->configured)) {
        // Try reading header directly from input. read_header handles
        // variable-length headers (1-2 bytes based on more_headers bit).
        // If the first byte indicates a 2-byte header but only 1 byte is
        // available, stash it and return INPUT_EXHAUSTED.
        size_t header_consumed;
        TampConf conf;
        if (TAMP_UNLIKELY(decompressor->header_bytes_read)) {
            // Second call: prepend stashed first byte.
            unsigned char header_buf[2] = {decompressor->stashed_header_byte, 0};
            if (input != input_end) header_buf[1] = *input;
            res = tamp_decompressor_read_header(&conf, header_buf, 1 + (input != input_end), &header_consumed);
            if (res != TAMP_OK) return res;
            // First byte was already consumed in prior call; only count new bytes.
            size_t new_consumed = header_consumed - 1;
            input += new_consumed;
            (*input_consumed_size) += new_consumed;
        } else {
            res = tamp_decompressor_read_header(&conf, input, input_end - input, &header_consumed);
            if (res == TAMP_INPUT_EXHAUSTED && input != input_end) {
                // Have first byte but need second; stash and retry next call.
                decompressor->stashed_header_byte = *input;
                decompressor->header_bytes_read = 1;
                (*input_consumed_size)++;
                return TAMP_INPUT_EXHAUSTED;
            }
            if (res != TAMP_OK) return res;
            input += header_consumed;
            (*input_consumed_size) += header_consumed;
        }

        res = tamp_decompressor_populate_from_conf(decompressor, conf.window, conf.literal, conf.use_custom_dictionary,
                                                   conf.extended, conf.dictionary_reset);
        if (res != TAMP_OK) return res;
        decompressor->skip_bytes = 0;  // Clear stale stashed_header_byte (shares union storage)
    }

    /* Snapshot after header parsing: *input_consumed_size holds the header
     * bytes, and every exit below adds (input - input_start) via
     * TAMP_DECOMP_RETURN. Header-error returns above happen before this and
     * account for their bytes directly. */
    const unsigned char* const input_start = input;

    // Cache bitfield values in local variables for faster access. With the
    // TAMP_FIXED_* build options these initializers are compile-time constants,
    // so every downstream window/literal shift and min_pattern_size use folds.
    const uint8_t conf_window = TAMP_CONF_WINDOW_INIT(decompressor);
    const uint8_t conf_literal = TAMP_CONF_LITERAL_INIT(decompressor);
    const uint8_t min_pattern_size = TAMP_CONF_MIN_PATTERN_INIT(decompressor);

    const uint16_t window_mask = (1 << conf_window) - 1;
#if TAMP_EXTENDED_DECOMPRESS
    const bool extended_enabled = decompressor->conf_extended;
#endif

    while (input != input_end || decompressor->pos_and_state) {
        if (TAMP_UNLIKELY(output == output_end)) TAMP_DECOMP_RETURN(TAMP_OUTPUT_FULL);

        // Populate the bit buffer
        refill_bit_buffer(decompressor, &input, input_end);

#if TAMP_EXTENDED_DECOMPRESS
        /* Handle extended tokens - either resuming or fresh from match_size detection below. */
        if (TAMP_UNLIKELY(decompressor->token_state)) {
        extended_dispatch:
            if (decompressor->token_state == TOKEN_RLE) {
                res = decode_rle(decompressor, &output, output_end);
            } else {
                res = decode_extended_match(decompressor, &output, output_end);
            }
            if (res == TAMP_INPUT_EXHAUSTED) {
                uint8_t old_bit_pos = decompressor->bit_buffer_pos;
                refill_bit_buffer(decompressor, &input, input_end);
                /* If we couldn't get more bits and input is exhausted, stop.
                 * Otherwise the loop would run forever with token_state set. */
                if (decompressor->bit_buffer_pos == old_bit_pos && input == input_end) {
                    TAMP_DECOMP_RETURN(TAMP_INPUT_EXHAUSTED);
                }
                continue;
            }
            if (res != TAMP_OK) TAMP_DECOMP_RETURN(res);
            continue;
        }
#endif  // TAMP_EXTENDED_DECOMPRESS

        if (TAMP_UNLIKELY(decompressor->bit_buffer_pos == 0)) TAMP_DECOMP_RETURN(TAMP_INPUT_EXHAUSTED);

#if TAMP_FAST_DECODE_LOOP
        /* Checked-once fast inner loop. Entered only when no callback and no
         * pending skip/extended/flush state; the per-iteration precondition
         * (>=4 input bytes so the unguarded refill never reaches input_end,
         * >=32 output bytes so no classic token can fill the buffer) makes
         * every mid-token bounds/exhaustion check on the classic path dead:
         *   - refill reaches bit_buffer_pos >= 25 (a classic token needs <= 24)
         *   - literal (<= 1+conf_literal <= 9 bits) and match window offset
         *     (<= conf_window <= 15 bits, after 1+huffman<=9) always present
         *   - output has room for any single token (match_size <= 17, lit = 1)
         * last_was_flush invariant: entry requires it 0; only FLUSH sets it and
         * FLUSH breaks out, so no per-token clear is needed in the body. On a
         * structural break (FLUSH sets last_was_flush, extended sets
         * token_state) we continue the outer loop so its refill + extended
         * dispatch / double-FLUSH handling own those cases; a precondition
         * failure (the final tokens of the buffer) falls through to the
         * careful body with clean, topped-up state - a dedicated single-token
         * tail loop was measured to add ~580 bytes of flash for no meaningful
         * speedup, so the careful body owns the tail. */
        if (callback == NULL && decompressor->skip_bytes == 0 && TAMP_PENDING_TOKEN_STATE(decompressor) == 0 &&
            decompressor->last_was_flush == 0) {
            res = tamp_fast_decode_loop(decompressor, &input, input_end, &output, output_end);
            if (TAMP_UNLIKELY(res != TAMP_OK)) TAMP_DECOMP_RETURN(res);
            if (TAMP_PENDING_TOKEN_STATE(decompressor) || decompressor->last_was_flush) continue;
        }
#endif /* TAMP_FAST_DECODE_LOOP */

        // Hint that patterns are more likely than literals
        if (TAMP_UNLIKELY(decompressor->bit_buffer >> 31)) {
            // is literal
            if (TAMP_UNLIKELY(decompressor->last_was_flush)) decompressor->last_was_flush = 0;
            if (TAMP_UNLIKELY(decompressor->bit_buffer_pos < (1 + conf_literal)))
                TAMP_DECOMP_RETURN(TAMP_INPUT_EXHAUSTED);
            /* Finish all struct-field updates before the unsigned-char stores to
             * output/window: char stores may alias the decompressor, so ordering
             * them last avoids forced reloads of the fields. */
            uint32_t bit_buffer = decompressor->bit_buffer << 1;  // shift out the is_literal flag
            uint8_t literal = bit_buffer >> (32 - conf_literal);
            decompressor->bit_buffer = bit_buffer << conf_literal;
            decompressor->bit_buffer_pos -= (1 + conf_literal);
            uint16_t wp = decompressor->window_pos;
            decompressor->window_pos = (wp + 1) & window_mask;

            decompressor->window[wp] = literal;
            *output++ = literal;
        } else {
            // is token; attempt a decode
            /* copy the bit buffers so that we can abort at any time */
            uint32_t bit_buffer = decompressor->bit_buffer;
            uint16_t window_offset;
            uint8_t bit_buffer_pos = decompressor->bit_buffer_pos;
            int8_t match_size;

            // shift out the is_literal flag
            bit_buffer <<= 1;
            bit_buffer_pos--;

            uint8_t match_size_u8;
            if (decode_huffman(&bit_buffer, &bit_buffer_pos, 0, &match_size_u8) != TAMP_OK)
                TAMP_DECOMP_RETURN(TAMP_INPUT_EXHAUSTED);
            match_size = match_size_u8;

            if (TAMP_UNLIKELY(match_size == FLUSH)) {
                // flush bit_buffer to the nearest byte and skip the remainder of decoding
                decompressor->bit_buffer = bit_buffer << (bit_buffer_pos & 7);
                decompressor->bit_buffer_pos =
                    bit_buffer_pos & ~7;  // Round bit_buffer_pos down to nearest multiple of 8.
                if (decompressor->conf_dictionary_reset && decompressor->last_was_flush) {
                    // Double-FLUSH: reset dictionary.
                    decompressor->window_pos = 0;
                    tamp_initialize_dictionary(decompressor->window, (size_t)1 << conf_window,
                                               decompressor->conf_extended ? conf_literal : 8);
                }
                decompressor->last_was_flush = 1;
                continue;
            }

            if (TAMP_UNLIKELY(decompressor->last_was_flush)) decompressor->last_was_flush = 0;

#if TAMP_EXTENDED_DECOMPRESS
            /* Check for extended symbols (RLE=12, extended match=13).
             * Convert match_size to token_state via subtraction (see TOKEN_* defines). */
            if (TAMP_UNLIKELY(extended_enabled && match_size >= TAMP_RLE_SYMBOL)) {
                decompressor->bit_buffer = bit_buffer;
                decompressor->bit_buffer_pos = bit_buffer_pos;
                decompressor->token_state = match_size - (TAMP_RLE_SYMBOL - 1);
                goto extended_dispatch;
            }
#endif  // TAMP_EXTENDED_DECOMPRESS

            if (TAMP_UNLIKELY(bit_buffer_pos < conf_window)) {
                // There are not enough bits to decode window offset
                TAMP_DECOMP_RETURN(TAMP_INPUT_EXHAUSTED);
            }
            match_size += min_pattern_size;
            window_offset = bit_buffer >> (32 - conf_window);

            // Security check: validate that the pattern reference (offset + size) does not
            // exceed window bounds. Malicious compressed data could craft out-of-bounds
            // references to read past the window buffer, potentially leaking memory.
            // Cast to uint32_t prevents signed integer overflow.
            const uint32_t window_size = (1u << conf_window);
            /* window_offset < window_size by construction (conf_window-bit extraction),
             * and match_size <= 30 << window_size, so the subtraction cannot underflow. */
            if (TAMP_UNLIKELY((uint32_t)window_offset > window_size - (uint32_t)match_size)) {
                TAMP_DECOMP_RETURN(TAMP_OOB);
            }

            // Apply skip_bytes. skip is nonzero only when resuming a token that
            // was cut off by a full output buffer (at most once per call), so the
            // common path uses match_size/window_offset directly and never
            // touches decompressor->skip_bytes (it is already 0).
            uint8_t skip = decompressor->skip_bytes;
            uint16_t copy_offset = window_offset;
            uint8_t copy_size = match_size;
            if (TAMP_UNLIKELY(skip)) {
                copy_offset = window_offset + skip;
                copy_size = match_size - skip;
            }

            // Check if we are output-buffer-limited, and if so set skip_bytes.
            // Next tamp_decompressor_decompress_cb we will re-decode the same
            // token, and skip the first skip_bytes of it.
            // Otherwise, update the decompressor buffers.
            size_t remaining = output_end - output;
            if (TAMP_UNLIKELY(copy_size > remaining)) {
                decompressor->skip_bytes = skip + remaining;
                copy_size = remaining;
                TAMP_COPY_TO_OUTPUT(output, decompressor->window + copy_offset, copy_size);
            } else {
                if (TAMP_UNLIKELY(skip)) decompressor->skip_bytes = 0;
                decompressor->bit_buffer = bit_buffer << conf_window;
                decompressor->bit_buffer_pos = bit_buffer_pos - conf_window;

                TAMP_COPY_TO_OUTPUT(output, decompressor->window + copy_offset, copy_size);

                uint16_t wp = decompressor->window_pos;
#if TAMP_WINDOW_FROM_OUTPUT
                if (TAMP_LIKELY(skip == 0)) {
                    /* output's last match_size bytes are the full match, a linear
                     * non-overlapping snapshot of window[window_offset..]. */
                    TAMP_WINDOW_WRITE_FROM_OUTPUT(decompressor->window, wp, output - match_size, match_size,
                                                  window_size, window_mask);
                } else {
                    /* Resume completion: output only holds the match tail; the
                     * window still holds the source. */
                    TAMP_WINDOW_COPY(decompressor->window, &wp, window_offset, match_size, window_mask);
                }
#elif !TAMP_ESP32
                if (TAMP_LIKELY(skip == 0)) {
                    wp = tamp_window_write_from_output_fn(decompressor->window, output - match_size, wp,
                                                          ((uint32_t)(uint8_t)match_size << 16) | window_mask);
                } else {
                    TAMP_WINDOW_COPY(decompressor->window, &wp, window_offset, match_size, window_mask);
                }
#else
                TAMP_WINDOW_COPY(decompressor->window, &wp, window_offset, match_size, window_mask);
#endif
                decompressor->window_pos = wp;
            }
        }
        /* callback is rare; compute the live consumed count only when it fires
         * (short-circuit keeps the callback==NULL fast path off the pointer).
         * *input_consumed_size still holds only the header base here, so
         * TAMP_DECOMP_RETURN's later add is not a double-count. */
        if (TAMP_UNLIKELY(callback && (res = callback(user_data, *input_consumed_size + (size_t)(input - input_start),
                                                      input_size))))
            TAMP_DECOMP_RETURN((tamp_res)res);
    }
    TAMP_DECOMP_RETURN(TAMP_INPUT_EXHAUSTED);
#undef TAMP_DECOMP_RETURN
}
#if TAMP_HAS_GCC_OPTIMIZE
#pragma GCC pop_options
#endif
#if TAMP_FAST_DECODE_LOOP
#undef TAMP_FAST_WINDOW_UPDATE_
#undef TAMP_RES_REFILL
#undef TAMP_RES_WRITEBACK
#undef TAMP_RES_EXT_BREAK_
#undef TAMP_RES_TOKEN_BODY
#endif

#if TAMP_STREAM

TAMP_OPTIMIZE_SIZE tamp_res tamp_decompress_stream(TampDecompressor* decompressor, tamp_read_t read_cb,
                                                   void* read_handle, tamp_write_t write_cb, void* write_handle,
                                                   size_t* input_consumed_size, size_t* output_written_size,
                                                   tamp_callback_t callback, void* user_data) {
    size_t input_consumed_size_proxy, output_written_size_proxy;
    if (!input_consumed_size) input_consumed_size = &input_consumed_size_proxy;
    if (!output_written_size) output_written_size = &output_written_size_proxy;
    *input_consumed_size = 0;
    *output_written_size = 0;

    unsigned char input_buffer[TAMP_STREAM_WORK_BUFFER_SIZE / 2];
    unsigned char output_buffer[TAMP_STREAM_WORK_BUFFER_SIZE / 2];
    const size_t input_buffer_size = sizeof(input_buffer);
    const size_t output_buffer_size = sizeof(output_buffer);

    size_t input_pos = 0;
    size_t input_available = 0;
    bool eof_reached = false;

    while (1) {
        if (input_available == 0 && !eof_reached) {
            int bytes_read = read_cb(read_handle, input_buffer, input_buffer_size);
            if (TAMP_UNLIKELY(bytes_read < 0)) return TAMP_READ_ERROR;
            eof_reached = (bytes_read == 0);
            input_pos = 0;
            input_available = bytes_read;
            *input_consumed_size += bytes_read;
        }

        size_t chunk_consumed, chunk_written;

        tamp_res res = tamp_decompressor_decompress(decompressor, output_buffer, output_buffer_size, &chunk_written,
                                                    input_buffer + input_pos, input_available, &chunk_consumed);
        if (TAMP_UNLIKELY(res < TAMP_OK)) return res;

        input_pos += chunk_consumed;
        input_available -= chunk_consumed;

        if (TAMP_LIKELY(chunk_written > 0)) {
            int bytes_written = write_cb(write_handle, output_buffer, chunk_written);
            if (TAMP_UNLIKELY(bytes_written < 0 || (size_t)bytes_written != chunk_written)) {
                return TAMP_WRITE_ERROR;
            }
            *output_written_size += chunk_written;
        }

        if (TAMP_UNLIKELY(res == TAMP_INPUT_EXHAUSTED && eof_reached)) break;

        if (TAMP_UNLIKELY(callback)) {
            int cb_res = callback(user_data, *input_consumed_size, 0);
            if (TAMP_UNLIKELY(cb_res)) return (tamp_res)cb_res;
        }
    }

    return TAMP_OK;
}

#endif /* TAMP_STREAM */
