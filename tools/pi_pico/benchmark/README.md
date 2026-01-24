# Pi Pico Benchmark

Benchmarks Tamp compression and decompression on the RP2040 (Raspberry Pi Pico).

## Prerequisites

- [Pico SDK](https://github.com/raspberrypi/pico-sdk) installed
- `PICO_SDK_PATH` environment variable set
- ARM toolchain (`arm-none-eabi-gcc`)
- Test data files in `build/`:
  - `build/enwik8-100kb` (raw test data)
  - `build/enwik8-100kb.tamp` (compressed test data)

## Building

```bash
cd tools/pi_pico/benchmark
mkdir -p build && cd build
cmake ..
make
```

This produces `tamp_benchmark.uf2`.

## Flashing

1. Hold the BOOTSEL button on your Pico
2. Connect USB while holding BOOTSEL
3. Run `make flash` from the build directory

## Output

Monitor via USB serial using `make monitor PORT=/dev/ttyACM0` (adjust port as
needed):

```
compression: 3510255 us, 28487 bytes/s
decompression: 165429 us, 604488 bytes/s
```

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
