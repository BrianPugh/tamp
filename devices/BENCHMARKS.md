# On-Device Benchmarks

All rows measure the same workload: compressing and decompressing the **first
100 KB of [enwik8](https://mattmahoney.net/dc/textdata.html)** with a 1 KB
window (`window=10`, `literal=8`), in the classic (v1, non-extended) stream
format. Throughput is input bytes per second for compression and output bytes
per second for decompression. Reproduce with the `*-device-benchmark` Makefile
targets (see the per-device directories here).

The "Tamp options" column lists the compile-time flags that reproduce that build
when using Tamp as a library (`—` = none: the portable defaults). The available
flags are documented in the platform tuning section of
`tamp/_c_src/tamp/common.h`; `TAMP_ESP32=1` additionally requires the espidf
component's platform sources.

"Code size (B)" [^codesize] is the sum of `text` (includes `.rodata`) across the
vendored objects, built with that row's exact flags.

| Device                                                                                | Core           | Clock   | Runtime / build                   | Tamp options              | Code size (B) | Compression (s) | Compression (bytes/s) | Decompression (s) | Decompression (bytes/s) |
| ------------------------------------------------------------------------------------- | -------------- | ------- | --------------------------------- | ------------------------- | ------------- | --------------- | --------------------- | ----------------- | ----------------------- |
| [Raspberry Pi Pico](https://www.raspberrypi.com/products/raspberry-pi-pico/) (RP2040) | Cortex-M0+     | 125 MHz | C, `-O3`                          | —                         | 6,655         | 2.25            | 44,523                | 0.081             | 1,234,004               |
| [Raspberry Pi Pico](https://www.raspberrypi.com/products/raspberry-pi-pico/) (RP2040) | Cortex-M0+     | 125 MHz | C, `-O3`                          | `TAMP_FAST_DECODE_LOOP=1` | 7,387         | 2.25            | 44,523                | 0.059             | 1,700,420               |
| [Raspberry Pi Pico](https://www.raspberrypi.com/products/raspberry-pi-pico/) (RP2040) | Cortex-M0+     | 125 MHz | MicroPython v1.26.1 native module | —                         | 5,883         | 2.90            | 34,510                | 0.102             | 980,392                 |
| ESP32                                                                                 | Xtensa LX6     | 160 MHz | ESP-IDF v6.0.2, `-O2`             | —                         | 6,581         | 1.756           | 56,900                | 0.068             | 1,470,000               |
| ESP32                                                                                 | Xtensa LX6     | 160 MHz | ESP-IDF v6.0.2, `-O2`             | `TAMP_ESP32=1`            | 8,374         | 1.708           | 58,500                | 0.067             | 1,490,000               |
| ESP32-S3                                                                              | Xtensa LX7     | 160 MHz | ESP-IDF v6.0.2, `-O2`             | —                         | 6,609         | 1.487           | 67,200                | 0.051             | 1,977,000               |
| ESP32-S3                                                                              | Xtensa LX7     | 160 MHz | ESP-IDF v6.0.2, `-O2`             | `TAMP_ESP32=1` (PIE SIMD) | 8,458         | 0.255           | 392,600               | 0.048             | 2,066,000               |
| ESP32-C3                                                                              | RISC-V RV32IMC | 160 MHz | ESP-IDF v6.0.2, `-O2`             | —                         | 7,215         | 1.413           | 70,800                | 0.040             | 2,500,000               |
| ESP32-C3                                                                              | RISC-V RV32IMC | 160 MHz | ESP-IDF v6.0.2, `-O2`             | `TAMP_ESP32=1`            | 10,561        | 0.961           | 104,100               | 0.036             | 2,777,800               |
| STM32H7B0 [^sram]                                                                     | Cortex-M7      | 280 MHz | C, `-O3`, I+D cache               | —                         | 6,339         | 0.518           | 192,900               | 0.015             | 6,746,000               |
| STM32H7B0 [^sram]                                                                     | Cortex-M7      | 280 MHz | C, `-O3`, I+D cache               | `TAMP_ARMV7EM=1`          | 10,687        | 0.395           | 253,000               | 0.008             | 12,875,000              |

[^sram]:
    The STM32H7B0's benchmark input/reference data resides in internal SRAM (the
    128 KB flash cannot embed it; the runner loads it over SWD), whereas the
    other devices read it from cached external/XiP flash. Code still runs from
    internal flash. This makes the data-read path somewhat faster than a
    flash-resident workload would be.

[^codesize]:
    Berkeley `size`'s `text` for `common.o + compressor.o + decompressor.o`
    (plus `compressor_esp32.o`, the espidf component's platform source, for
    `TAMP_ESP32=1` rows), compiled standalone with that row's CPU/opt/flags - no
    linking, no `--gc-sections`. Reproduce with `tools/benchmark-code-size.sh`
    (also `make benchmark-code-sizes`).

New device targets (e.g. RP2350, STM32H7) should add rows here using the same
100 KB enwik8 workload and a 10-bit window.
