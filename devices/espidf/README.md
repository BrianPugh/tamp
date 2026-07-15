# ESP32 On-Device Harness

Benchmarks and correctness-tests tamp on real ESP32 silicon, consuming the local
`espidf/tamp/` component (not the registry version). The harness logic is shared
across devices in `../common/tamp_bench.c`; `main/main.c` is a thin platform
shell. Run everything from the repo root via the top-level `Makefile`.

## Prerequisites

- An activated esp-idf environment (`idf.py` on `PATH`, version >= 5.0).
- `uv` for host tooling (data staging, serial runner).
- The enwik8 dataset (`make download-enwik8`; auto-triggered by data staging).

## Targets

| Target                        | Action                                                                                    |
| ----------------------------- | ----------------------------------------------------------------------------------------- |
| `make esp32-device-data`      | Stage embedded data into `main/data/` (enwik8 blocks + packed vectors). No idf.py needed. |
| `make esp32-device-build`     | Stage data, stage the component, and build the app.                                       |
| `make esp32-device-test`      | Build, flash, and run; exits non-zero on device failure.                                  |
| `make esp32-device-benchmark` | Same as test, plus a `BENCH`/`INFO` summary.                                              |

## Variables

- `ESP32_PORT` — serial port; required for flash/test/benchmark (no default).
- `ESP32_TARGET` — chip target (default `esp32s3`; e.g. `esp32`, `esp32c3`).
- `TAMP_ESP32_OPT` — `y` (default) builds with TAMP_ESP32 optimizations; `n`
  builds the `TAMP_ESP32=n` variant into a separate build dir
  (`build-<target>-noopt`) by appending `sdkconfig.noopt`.

Example:

```
make esp32-device-test ESP32_PORT=/dev/ttyUSB0 ESP32_TARGET=esp32c3 TAMP_ESP32_OPT=n
```

## Output

The harness prints `PASS:`/`FAIL:` lines, machine-parseable
`BENCH <name>_us=<n>` and `INFO <key>=<value>` lines, and a final sentinel:

```
TAMP-DEVICE-RESULT: PASS
TAMP-DEVICE-RESULT: FAIL failures=<n>
```

The serial runner (`tools/device-runner.py`) resets the device, echoes output,
and exits 0 on PASS, 1 on FAIL, 2 on timeout.

## Regression vectors

`../vectors/` holds committed byte streams (shared by all device harnesses) that
are packed into `main/data/vectors.bin` and replayed through a fresh
decompressor on device (any result is acceptable; the check is that decoding
does not crash or hang). To add a case — for instance a host-fuzzer crash file
from `fuzz/corpus_*/crashes/` — drop the raw file into `devices/vectors/` and
rebuild. Keep seed vectors small.
