# On-Device Benchmarks

All rows measure the same workload: compressing and decompressing the **first
100 KB of [enwik8](https://mattmahoney.net/dc/textdata.html)** with a 1 KB
window (`window=10`, `literal=8`). Throughput is input bytes per second for
compression and output bytes per second for decompression. Reproduce with the
`*-device-benchmark` Makefile targets (see the per-device directories here).

In the "Tamp options" column, `—` is the platform's default build (Tamp selects
the best measured configuration per architecture; see the platform tuning
section in `tamp/_c_src/tamp/common.h`); other rows state the explicit options
that deviate from it.

| Device                                                                                | Core           | Clock   | Runtime / build                   | Tamp options               | Compression (s) | Compression (bytes/s) | Decompression (s) | Decompression (bytes/s) |
| ------------------------------------------------------------------------------------- | -------------- | ------- | --------------------------------- | -------------------------- | --------------- | --------------------- | ----------------- | ----------------------- |
| [Raspberry Pi Pico](https://www.raspberrypi.com/products/raspberry-pi-pico/) (RP2040) | Cortex-M0+     | 125 MHz | C, `-O3`                          | —                          | 2.77            | 36,127                | 0.071             | 1,400,600               |
| [Raspberry Pi Pico](https://www.raspberrypi.com/products/raspberry-pi-pico/) (RP2040) | Cortex-M0+     | 125 MHz | MicroPython v1.26.1 native module | —                          | 2.90            | 34,510                | 0.102             | 980,392                 |
| ESP32                                                                                 | Xtensa LX6     | 160 MHz | ESP-IDF v6.0.2, `-O2`             | `CONFIG_TAMP_ESP32=n`      | 1.756           | 56,900                | 0.068             | 1,470,000               |
| ESP32                                                                                 | Xtensa LX6     | 160 MHz | ESP-IDF v6.0.2, `-O2`             | — (`TAMP_ESP32` default)   | 1.708           | 58,500                | 0.067             | 1,490,000               |
| ESP32-S3                                                                              | Xtensa LX7     | 160 MHz | ESP-IDF v6.0.2, `-O2`             | `CONFIG_TAMP_ESP32=n`      | 1.487           | 67,200                | 0.052             | 1,912,000               |
| ESP32-S3                                                                              | Xtensa LX7     | 160 MHz | ESP-IDF v6.0.2, `-O2`             | — (`TAMP_ESP32` PIE SIMD)  | 0.255           | 392,600               | 0.050             | 1,987,000               |
| ESP32-C3                                                                              | RISC-V RV32IMC | 160 MHz | ESP-IDF v6.0.2, `-O2`             | `CONFIG_TAMP_ESP32=n`      | 1.413           | 70,800                | 0.040             | 2,500,000               |
| ESP32-C3                                                                              | RISC-V RV32IMC | 160 MHz | ESP-IDF v6.0.2, `-O2`             | — (`TAMP_ESP32` default)   | 0.961           | 104,100               | 0.036             | 2,777,800               |
| STM32H7B0 [^sram]                                                                     | Cortex-M7      | 280 MHz | C, `-O3`, I+D cache               | — (`TAMP_ARMV7EM` default) | 0.395           | 253,000               | 0.015             | 6,732,000               |
| STM32H7B0 [^sram]                                                                     | Cortex-M7      | 280 MHz | C, `-O3`, I+D cache               | `TAMP_ARMV7EM=0`           | 0.518           | 192,900               | 0.015             | 6,574,000               |

[^sram]:
    The STM32H7B0's benchmark input/reference data resides in internal SRAM (the
    128 KB flash cannot embed it; the runner loads it over SWD), whereas the
    other devices read it from cached external/XiP flash. Code still runs from
    internal flash. This makes the data-read path somewhat faster than a
    flash-resident workload would be.

New device targets (e.g. RP2350, STM32H7) should add rows here using the same
100 KB enwik8 workload and a 10-bit window.
