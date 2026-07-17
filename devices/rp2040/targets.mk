# Root-Makefile targets for the RP2040 harness (included from the repo root;
# run all targets from there). Builds the pico-sdk C benchmark in
# devices/rp2040/. Flashing is manual (BOOTSEL); see devices/rp2040/README.md.
#
# Variables:
#     RP2040_PORT   Serial port of the running benchmark (required for benchmark)
#     RP2040_TAMP_OPT  Extra tamp compile definitions (semicolon-separated),
#                      e.g. RP2040_TAMP_OPT="TAMP_FAST_DECODE_LOOP=1" for the
#                      fastloop BENCHMARKS.md row. Empty = portable defaults.
.PHONY: rp2040-device-build rp2040-device-flash rp2040-device-benchmark rp2040-device-help

rp2040-device-build: build/enwik8-100kb build/enwik8-100kb-v1.tamp device-vectors
	@[ -n "$$PICO_SDK_PATH" ] || { echo "Error: PICO_SDK_PATH is not set."; exit 1; }
	cmake -B devices/rp2040/build -S devices/rp2040 -DTAMP_BENCH_DEFINES="$(RP2040_TAMP_OPT)"
	$(MAKE) -C devices/rp2040/build tamp_benchmark

rp2040-device-flash: rp2040-device-build
	@[ -d /Volumes/RPI-RP2 ] || { echo "Error: /Volumes/RPI-RP2 not mounted - hold BOOTSEL while plugging in the Pico."; exit 1; }
	cp devices/rp2040/build/tamp_benchmark.uf2 /Volumes/RPI-RP2/

rp2040-device-benchmark:
	@[ -n "$(RP2040_PORT)" ] || { echo "Error: RP2040_PORT is not set (e.g. RP2040_PORT=/dev/tty.usbmodem...)."; exit 1; }
	uv run python tools/device-runner.py --port $(RP2040_PORT) --benchmark

DEVICE_HELP_TARGETS += rp2040-device-help
rp2040-device-help:
	@echo "RP2040 on-device benchmark (requires PICO_SDK_PATH):"
	@echo "  make rp2040-device-build      Build the pico-sdk benchmark UF2"
	@echo "  make rp2040-device-flash      Copy UF2 to a BOOTSEL-mounted Pico"
	@echo "  make rp2040-device-benchmark  Capture a benchmark run (needs RP2040_PORT)"
	@echo ""
