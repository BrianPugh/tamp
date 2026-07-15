/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "tamp/compressor.h"
#include "tamp/decompressor.h"

extern const uint8_t enwik8_100kb_start[] asm("_binary_enwik8_100kb_start");
extern const uint8_t enwik8_100kb_end[] asm("_binary_enwik8_100kb_end");

extern const uint8_t enwik8_100kb_tamp_start[] asm("_binary_enwik8_100kb_tamp_start");
extern const uint8_t enwik8_100kb_tamp_end[] asm("_binary_enwik8_100kb_tamp_end");

extern const uint8_t vectors_start[] asm("_binary_vectors_bin_start");
extern const uint8_t vectors_end[] asm("_binary_vectors_bin_end");

#define WINDOW_BITS 10
#define STRESS_BLOCK_SIZE 8192
#define STRESS_ITERATIONS 10
#define STRESS_SEED 0x1234abcdu

/* Sized for the largest window used anywhere below (the stress block's w=12). */
uint8_t window_buffer[1 << 12];
uint8_t compressed_buffer[60000];
uint8_t decompressed_buffer[100000];

static int failures = 0;

#define REPORT(ok, ...)                     \
    do {                                    \
        printf((ok) ? "PASS: " : "FAIL: "); \
        printf(__VA_ARGS__);                \
        printf("\n");                       \
        if (!(ok)) failures++;              \
    } while (0)

static uint32_t xs_next(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static const char STRESS_ALPHABET[] = "abcdefghijklmnop";

/* Deterministic pseudo-random content generator.
 * Fills `out` when `verify` is NULL; otherwise regenerates the same stream and
 * returns the number of bytes that differ from `verify`. State advances
 * identically in both modes, so a snapshot of the pre-generation state can
 * reproduce the stream without keeping a second copy. */
static size_t stress_stream(int generator, size_t size, uint32_t *state, uint8_t *out, const uint8_t *verify) {
    size_t mismatches = 0;
    for (size_t i = 0; i < size; i++) {
        uint8_t b;
        switch (generator) {
            case 0:
                b = (uint8_t)xs_next(state);
                break;
            case 1:
                b = (uint8_t)STRESS_ALPHABET[xs_next(state) & 0x0f];
                break;
            default:
                if ((i % 50) == 0)
                    b = (uint8_t)xs_next(state);
                else
                    b = (uint8_t)(((i & 63) * 37 + 11) & 0xff);
                break;
        }
        if (verify) {
            if (verify[i] != b) mismatches++;
        } else {
            out[i] = b;
        }
    }
    return mismatches;
}

void app_main(void) {
    uint64_t start, end;
    {
        /* Chip information */
        esp_chip_info_t chip_info;
        uint32_t flash_size;
        esp_chip_info(&chip_info);
        printf("This is %s chip with %d CPU core(s), %s%s%s%s, ", CONFIG_IDF_TARGET, chip_info.cores,
               (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
               (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "", (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
               (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");
        unsigned major_rev = chip_info.revision / 100;
        unsigned minor_rev = chip_info.revision % 100;
        printf("silicon revision v%d.%d, ", major_rev, minor_rev);
        if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
            printf("Get flash size failed\n");
            return;
        }
        printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
               (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
        printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());
#ifdef CONFIG_TAMP_ESP32
        printf("INFO tamp_esp32=enabled\n");
#else
        printf("INFO tamp_esp32=disabled\n");
#endif
    }

    size_t compressed_size = 0;
    {
        /* Compress enwik8-100kb and compare against the embedded reference. */
        size_t output_written_size;
        tamp_res res;
        TampCompressor compressor;
        TampConf conf = {.window = WINDOW_BITS, .literal = 8, .use_custom_dictionary = false};

        res = tamp_compressor_init(&compressor, &conf, window_buffer);
        REPORT(res == TAMP_OK, "compressor_init enwik8 (res=%d)", res);

        start = esp_timer_get_time();
        res = tamp_compressor_compress_and_flush(&compressor, compressed_buffer, sizeof(compressed_buffer),
                                                 &output_written_size, enwik8_100kb_start, 100000, NULL, false);
        end = esp_timer_get_time();
        compressed_size = output_written_size;
        printf("BENCH compress_enwik8_us=%lld\n", end - start);
        printf("INFO compressed_size=%zu\n", output_written_size);
        REPORT(res == TAMP_OK, "compress enwik8 (res=%d)", res);

        size_t expected_size = enwik8_100kb_tamp_end - enwik8_100kb_tamp_start;
        bool ok = (output_written_size == expected_size) &&
                  (memcmp(compressed_buffer, enwik8_100kb_tamp_start, expected_size) == 0);
        if (!ok) {
            if (output_written_size != expected_size)
                printf("  size mismatch: expected %zu, got %zu\n", expected_size, output_written_size);
            size_t min_size = expected_size < output_written_size ? expected_size : output_written_size;
            for (size_t i = 0; i < min_size; i++) {
                if (compressed_buffer[i] != enwik8_100kb_tamp_start[i]) {
                    printf("  first difference at %zu: expected 0x%02x, got 0x%02x\n", i, enwik8_100kb_tamp_start[i],
                           compressed_buffer[i]);
                    break;
                }
            }
        }
        REPORT(ok, "compress enwik8 matches reference");
    }

    {
        /* Decompress the embedded reference and compare against the original. */
        size_t output_written_size;
        tamp_res res;
        memset(decompressed_buffer, 0, sizeof(decompressed_buffer));
        TampDecompressor decompressor;
        res = tamp_decompressor_init(&decompressor, NULL, window_buffer, WINDOW_BITS);
        REPORT(res == TAMP_OK, "decompressor_init enwik8 (res=%d)", res);

        start = esp_timer_get_time();
        tamp_decompressor_decompress(&decompressor, decompressed_buffer, sizeof(decompressed_buffer),
                                     &output_written_size, enwik8_100kb_tamp_start,
                                     enwik8_100kb_tamp_end - enwik8_100kb_tamp_start, NULL);
        end = esp_timer_get_time();
        printf("BENCH decompress_enwik8_us=%lld\n", end - start);

        bool ok = (output_written_size == 100000) && (memcmp(decompressed_buffer, enwik8_100kb_start, 100000) == 0);
        REPORT(ok, "decompress reference matches original");
    }

    {
        /* Round-trip: decompress our own compressed output. */
        size_t output_written_size;
        memset(decompressed_buffer, 0, sizeof(decompressed_buffer));
        TampDecompressor decompressor;
        tamp_decompressor_init(&decompressor, NULL, window_buffer, WINDOW_BITS);
        tamp_decompressor_decompress(&decompressor, decompressed_buffer, sizeof(decompressed_buffer),
                                     &output_written_size, compressed_buffer, compressed_size, NULL);

        bool ok = (output_written_size == 100000) && (memcmp(decompressed_buffer, enwik8_100kb_start, 100000) == 0);
        if (!ok && output_written_size != 100000)
            printf("  size mismatch: expected 100000, got %zu\n", output_written_size);
        REPORT(ok, "round-trip enwik8");
    }

    {
        /* Repetitive synthetic data (long/extended matches dominate). */
        const size_t repetitive_size = sizeof(decompressed_buffer);
#define REPETITIVE_BYTE(i) ((i) % 257 == 0 ? '!' : (uint8_t)('A' + ((i) % 61)))
        for (size_t i = 0; i < repetitive_size; i++) decompressed_buffer[i] = REPETITIVE_BYTE(i);

        size_t output_written_size;
        tamp_res res;
        TampCompressor compressor;
        TampConf conf = {.window = WINDOW_BITS, .literal = 8, .use_custom_dictionary = false};
        tamp_compressor_init(&compressor, &conf, window_buffer);
        start = esp_timer_get_time();
        res =
            tamp_compressor_compress_and_flush(&compressor, compressed_buffer, sizeof(compressed_buffer),
                                               &output_written_size, decompressed_buffer, repetitive_size, NULL, false);
        end = esp_timer_get_time();
        printf("BENCH compress_repetitive_us=%lld\n", end - start);
        REPORT(res == TAMP_OK, "compress repetitive (res=%d)", res);

        size_t comp_size = output_written_size;
        memset(decompressed_buffer, 0, sizeof(decompressed_buffer));
        TampDecompressor decompressor;
        tamp_decompressor_init(&decompressor, NULL, window_buffer, WINDOW_BITS);
        start = esp_timer_get_time();
        tamp_decompressor_decompress(&decompressor, decompressed_buffer, sizeof(decompressed_buffer),
                                     &output_written_size, compressed_buffer, comp_size, NULL);
        end = esp_timer_get_time();
        printf("BENCH decompress_repetitive_us=%lld\n", end - start);

        bool ok = (output_written_size == repetitive_size);
        for (size_t i = 0; ok && i < repetitive_size; i++) {
            if (decompressed_buffer[i] != REPETITIVE_BYTE(i)) ok = false;
        }
        REPORT(ok, "round-trip repetitive");
#undef REPETITIVE_BYTE
    }

    {
        /* Vector replay: feed each embedded regression vector to a fresh
         * decompressor. Any tamp_res is acceptable; we only require no crash/hang. */
        const uint8_t *vp = vectors_start;
        uint32_t count = 0;
        if ((size_t)(vectors_end - vectors_start) >= sizeof(count)) {
            memcpy(&count, vp, sizeof(count));
            vp += sizeof(count);
        }
        for (uint32_t i = 0; i < count; i++) {
            uint32_t len = 0;
            memcpy(&len, vp, sizeof(len));
            vp += sizeof(len);
            const uint8_t *data = vp;
            vp += len;

            TampDecompressor decompressor;
            tamp_decompressor_init(&decompressor, NULL, window_buffer, WINDOW_BITS);
            const uint8_t *input = data;
            size_t remaining = len;
            tamp_res res = TAMP_OK;
            for (;;) {
                size_t consumed = 0, written = 0;
                res = tamp_decompressor_decompress(&decompressor, decompressed_buffer, sizeof(decompressed_buffer),
                                                   &written, input, remaining, &consumed);
                input += consumed;
                remaining -= consumed;
                if (res != TAMP_OK && res != TAMP_INPUT_EXHAUSTED && res != TAMP_OUTPUT_FULL) break;
                if (remaining == 0 && res == TAMP_INPUT_EXHAUSTED) break;
                if (consumed == 0 && written == 0) break;
            }
            printf("PASS: vector %" PRIu32 " (res=%d)\n", i, res);
        }
    }

    {
        /* Seeded PRNG stress: compress/decompress deterministic pseudo-random
         * content across several window sizes and content shapes. */
        uint32_t stress_state = STRESS_SEED;
        printf("INFO stress_seed=%u\n", (unsigned)STRESS_SEED);
        static const uint8_t window_bits_set[3] = {8, 10, 12};
        for (int wi = 0; wi < 3; wi++) {
            uint8_t wb = window_bits_set[wi];
            for (int gen = 0; gen < 3; gen++) {
                bool combo_ok = true;
                for (int iter = 0; iter < STRESS_ITERATIONS; iter++) {
                    uint32_t gen_state = stress_state;
                    stress_stream(gen, STRESS_BLOCK_SIZE, &stress_state, decompressed_buffer, NULL);

                    tamp_res res;
                    TampCompressor compressor;
                    TampConf conf = {.window = wb, .literal = 8, .use_custom_dictionary = false};
                    res = tamp_compressor_init(&compressor, &conf, window_buffer);
                    size_t comp_size = 0;
                    if (res == TAMP_OK)
                        res = tamp_compressor_compress_and_flush(&compressor, compressed_buffer,
                                                                 sizeof(compressed_buffer), &comp_size,
                                                                 decompressed_buffer, STRESS_BLOCK_SIZE, NULL, false);

                    size_t decomp_size = 0;
                    if (res == TAMP_OK) {
                        TampDecompressor decompressor;
                        tamp_decompressor_init(&decompressor, NULL, window_buffer, wb);
                        tamp_decompressor_decompress(&decompressor, decompressed_buffer, sizeof(decompressed_buffer),
                                                     &decomp_size, compressed_buffer, comp_size, NULL);
                    }

                    bool ok = (res == TAMP_OK) && (decomp_size == STRESS_BLOCK_SIZE);
                    if (ok) {
                        uint32_t verify_state = gen_state;
                        ok = stress_stream(gen, STRESS_BLOCK_SIZE, &verify_state, NULL, decompressed_buffer) == 0;
                    }
                    if (!ok) {
                        combo_ok = false;
                        printf("FAIL: stress seed=%u w=%d gen=%d iter=%d (res=%d)\n", (unsigned)STRESS_SEED, wb, gen,
                               iter, res);
                        failures++;
                    }
                }
                if (combo_ok) printf("PASS: stress w=%d gen=%d (%d iters)\n", wb, gen, STRESS_ITERATIONS);
            }
        }
    }

    if (failures == 0)
        printf("TAMP-DEVICE-RESULT: PASS\n");
    else
        printf("TAMP-DEVICE-RESULT: FAIL failures=%d\n", failures);
    fflush(stdout);

    for (;;) vTaskDelay(1000 / portTICK_PERIOD_MS);
}
