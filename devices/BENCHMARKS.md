# On-Device Benchmarks

All rows measure the same workload: compressing and decompressing the **first
100 KB of [enwik8](https://mattmahoney.net/dc/textdata.html)** with a 1 KB
window (`window=10`, `literal=8`). Throughput is input bytes per second for
compression and output bytes per second for decompression. Reproduce with the
`*-device-benchmark` Makefile targets (see the per-device directories here).

| Device                                                                                | Core           | Clock   | Runtime / build                   | Tamp options            | Compression (s) | Compression (bytes/s) | Decompression (s) | Decompression (bytes/s) |
| ------------------------------------------------------------------------------------- | -------------- | ------- | --------------------------------- | ----------------------- | --------------- | --------------------- | ----------------- | ----------------------- |
| [Raspberry Pi Pico](https://www.raspberrypi.com/products/raspberry-pi-pico/) (RP2040) | Cortex-M0+     | 125 MHz | C, `-O3`                          | —                       | 1.52            | 65,745                | 0.068             | 1,475,339               |
| [Raspberry Pi Pico](https://www.raspberrypi.com/products/raspberry-pi-pico/) (RP2040) | Cortex-M0+     | 125 MHz | MicroPython v1.26.1 native module | —                       | 2.90            | 34,510                | 0.102             | 980,392                 |
| ESP32                                                                                 | Xtensa LX6     | 160 MHz | ESP-IDF v6.0.2, `-O2`             | —                       | 1.511           | 66,200                | 0.058             | 1,728,000               |
| ESP32                                                                                 | Xtensa LX6     | 160 MHz | ESP-IDF v6.0.2, `-O2`             | `TAMP_ESP32`            | 1.701           | 58,800                | 0.062             | 1,619,000               |
| ESP32-S3                                                                              | Xtensa LX7     | 160 MHz | ESP-IDF v6.0.2, `-O2`             | —                       | 1.221           | 81,900                | 0.046             | 2,185,000               |
| ESP32-S3                                                                              | Xtensa LX7     | 160 MHz | ESP-IDF v6.0.2, `-O2`             | `TAMP_ESP32` (PIE SIMD) | 0.250           | 399,800               | 0.048             | 2,100,000               |
| ESP32-C3                                                                              | RISC-V RV32IMC | 160 MHz | ESP-IDF v6.0.2, `-O2`             | —                       | 1.175           | 85,100                | 0.031             | 3,198,000               |
| ESP32-C3                                                                              | RISC-V RV32IMC | 160 MHz | ESP-IDF v6.0.2, `-O2`             | `TAMP_ESP32`            | 0.952           | 105,000               | 0.031             | 3,176,000               |
| STM32H7B0 [^sram]                                                                     | Cortex-M7      | 280 MHz | C, `-O3`, I+D cache               | —                       | 0.463           | 215,900               | 0.012             | 8,645,000               |

[^sram]:
    The STM32H7B0's benchmark input/reference data resides in internal SRAM (the
    128 KB flash cannot embed it; the runner loads it over SWD), whereas the
    other devices read it from cached external/XiP flash. Code still runs from
    internal flash. This makes the data-read path somewhat faster than a
    flash-resident workload would be.

New device targets (e.g. RP2350, STM32H7) should add rows here using the same
100 KB enwik8 workload and a 10-bit window.
