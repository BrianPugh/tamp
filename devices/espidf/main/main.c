#include <inttypes.h>
#include <stdio.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "tamp_bench.h"

extern const uint8_t enwik8_100kb_start[] asm("_binary_enwik8_100kb_start");
extern const uint8_t enwik8_100kb_end[] asm("_binary_enwik8_100kb_end");

extern const uint8_t enwik8_100kb_tamp_start[] asm("_binary_enwik8_100kb_tamp_start");
extern const uint8_t enwik8_100kb_tamp_end[] asm("_binary_enwik8_100kb_tamp_end");

extern const uint8_t vectors_start[] asm("_binary_vectors_bin_start");
extern const uint8_t vectors_end[] asm("_binary_vectors_bin_end");

uint64_t tamp_bench_time_us(void) { return (uint64_t)esp_timer_get_time(); }

void app_main(void) {
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

    TampBenchData data = {
        .input = enwik8_100kb_start,
        .input_size = enwik8_100kb_end - enwik8_100kb_start,
        .reference = enwik8_100kb_tamp_start,
        .reference_size = enwik8_100kb_tamp_end - enwik8_100kb_tamp_start,
        .vectors = vectors_start,
        .vectors_size = vectors_end - vectors_start,
        .stress_iterations = 0,
    };
    tamp_bench_run(&data);

    for (;;) vTaskDelay(1000 / portTICK_PERIOD_MS);
}
