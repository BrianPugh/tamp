# STM32H7B0 On-Device Harness

Bare-metal test/benchmark harness for STM32H7B0 boards (tested: EC Buying
STM32H7B0VBT6, a WeAct MiniSTM32H7B0 clone: 25 MHz HSE crystal, LDO supply, 8 MB
QSPI flash unused by this harness). Runs the shared device harness
(`../common/tamp_bench.c`) compiled `-O3`: enwik8-100 KB benchmarks with
byte-verification against the v1 (non-extended) reference, replay of the shared
regression vectors (`../vectors/`), and seeded PRNG round-trip stress. Run
everything from the repo root.

No STM32Cube/HAL: register definitions are from ST's BSD-licensed CMSIS headers,
automatically fetched via `curl` into `build/cmsis/` (pinned tags
`cmsis_device_h7` v1.10.7, `CMSIS_5` 5.9.0) on first build.

## Prerequisites

- `arm-none-eabi-gcc`
- `openocd`
- An ST-Link (any SWD-capable version) wired to SWDIO, SWCLK, GND, and 3.3 V

## Usage

```bash
make stm32h7b0-device-build # Build devices/stm32h7b0/build/tamp_benchmark.elf
make stm32h7b0-device-test # Flash via OpenOCD, run full suite; exit 0/1/2 = pass/fail/timeout
make stm32h7b0-device-benchmark # Same, plus a BENCH/INFO summary block at the end
```

No port variable is needed; OpenOCD auto-detects the ST-Link.

The firmware configures the chip itself: LDO supply + VOS0, 280 MHz from PLL1
(25 MHz HSE, M=5 N=112 P=2), flash latency 7, I+D caches, and a 1 MHz TIM2
timebase. It prints `INFO` lines with register readbacks plus a DWT-vs-TIM2
`measured_cpu_mhz` cross-check, and falls back to the 64 MHz HSI (reported in
the output) if the HSE or PLL fails to lock.

The 128 KB internal flash cannot embed the ~150 KB benchmark data, so the
firmware is code-only. `make stm32h7b0-device-*` packs
`build/stm32h7b0-bench-data.bin` (via `tools/pack-device-blob.py`) and uses
`devices/stm32h7b0/run.tcl` to load it into AXI SRAM at `0x24000000` over SWD
before resuming the target.

Output is via ARM semihosting through OpenOCD (no UART).
`tools/device-runner.py --exec` parses the `TAMP-DEVICE-RESULT:` sentinel; exit
code 0 = pass, 1 = fail, 2 = timeout.

The firmware runs single-shot and idles after printing the sentinel. It must be
started via the `make` targets / `run.tcl` - standalone runs will hard-fault on
the semihosting `BKPT` and find no data blob.

On-device byte-equality references must be v1 non-extended format generated with
`--implementation=python`. This build compresses classic format without
`TAMP_LAZY_MATCHING`.

## Profiling

The harness includes profiling infrastructure via `../common/profiling.h`
(available on every device port). Stats are always printed after decompression
(showing zeros if no instrumentation is added).

To profile specific sections, temporarily add instrumentation to
`tamp/_c_src/tamp/*.c`:

```c
// At the top of the file, after other includes:
#include "profiling.h"

// Around code sections you want to measure:
TAMP_PROFILE_START_NAMED(section_name);
// ... code to measure ...
TAMP_PROFILE_END_NAMED(section_name, tamp_profiling_stats.some_accumulator);
tamp_profiling_stats.some_counter++;
```

Remove all instrumentation from library code before committing.

### Available macros

```c
// Simple timing (one timer active)
TAMP_PROFILE_START()
TAMP_PROFILE_END(accumulator)

// Named timing (multiple timers can be active)
TAMP_PROFILE_START_NAMED(name)
TAMP_PROFILE_END_NAMED(name, accumulator)
```

### TampProfilingStats fields

| Field                     | Description                          |
| ------------------------- | ------------------------------------ |
| `literal_count`           | Number of literal bytes processed    |
| `pattern_count`           | Number of pattern matches processed  |
| `pattern_bytes_total`     | Total bytes from pattern matches     |
| `overlap_count`           | Pattern copies where src/dst overlap |
| `non_overlap_count`       | Pattern copies without overlap       |
| `literal_time_us`         | Time in literal processing           |
| `pattern_decode_time_us`  | Time decoding pattern tokens         |
| `pattern_output_time_us`  | Time copying patterns to output      |
| `window_update_time_us`   | Time updating the window buffer      |
| `bit_buffer_fill_time_us` | Time filling bit buffer from input   |

Results are collected in `devices/BENCHMARKS.md`.
