# RP2040 On-Device Benchmark

Pico-sdk application that benchmarks tamp C compression/decompression of the
first 100 KB of enwik8 on an RP2040 (Raspberry Pi Pico), compiled `-O3` with no
filesystem or dynamic allocation. Output byte-compared against the embedded v1
(non-extended) reference. Run everything from the repo root.

## Prerequisites

- [Pico SDK](https://github.com/raspberrypi/pico-sdk) with `PICO_SDK_PATH` set
- ARM toolchain (`arm-none-eabi-gcc`)

## Usage

```bash
make rp2040-device-build       # Build devices/rp2040/build/tamp_benchmark.uf2
make rp2040-device-flash       # Copy UF2 to a BOOTSEL-mounted Pico (/Volumes/RPI-RP2)
make rp2040-device-benchmark RP2040_PORT=/dev/tty.usbmodem...   # Capture a run
```

To flash: hold BOOTSEL while connecting USB, then `make rp2040-device-flash`.
The Pico reboots into the benchmark, which loops forever printing `BENCH`/`INFO`
lines, `PASS:`/`FAIL:` checks, and a `TAMP-DEVICE-RESULT:` sentinel each
iteration; `make rp2040-device-benchmark` captures one iteration and exits
nonzero on failure.

## Profiling

The benchmark includes profiling infrastructure via `profiling.h`. Stats are
always printed after decompression (showing zeros if no instrumentation is
added).

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
