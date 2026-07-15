# Root-Makefile targets for the ESP32 harness (included from the repo root;
# run all targets from there). Build, flash, and run the ESP-IDF
# benchmark/test harness in devices/espidf/.
#
# Variables:
#     ESP32_PORT      Serial port (required for flash/test/benchmark; no default)
#     ESP32_TARGET    Chip target (default: esp32s3; e.g. esp32, esp32c3)
#     TAMP_ESP32_OPT  y (default) uses TAMP_ESP32 optimizations; n builds the
#                     TAMP_ESP32=n variant into a separate build dir
.PHONY: esp32-device-data esp32-device-build esp32-device-test esp32-device-benchmark esp32-device-help

ESP32_TARGET ?= esp32s3
TAMP_ESP32_OPT ?= y

ifeq ($(TAMP_ESP32_OPT),n)
ESP32_BUILD_NAME := build-$(ESP32_TARGET)-noopt
ESP32_SDKCONFIG_ARG := -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.noopt"
else
ESP32_BUILD_NAME := build-$(ESP32_TARGET)
ESP32_SDKCONFIG_ARG :=
endif
# idf.py resolves -B relative to the CWD but SDKCONFIG relative to the project dir.
ESP32_BUILD_DIR := devices/espidf/$(ESP32_BUILD_NAME)

# Stage embedded data (enwik8 blocks + packed regression vectors). Does NOT
# require idf.py, so CI can generate data before invoking the IDF build action.
esp32-device-data: build/enwik8-100kb build/enwik8-100kb-v1.tamp device-vectors
	@mkdir -p devices/espidf/main/data
	cp build/enwik8-100kb devices/espidf/main/data/enwik8-100kb
	cp build/enwik8-100kb-v1.tamp devices/espidf/main/data/enwik8-100kb.tamp
	cp build/device-vectors.bin devices/espidf/main/data/vectors.bin

esp32-device-build: esp32-device-data
	@command -v idf.py >/dev/null 2>&1 || { echo "Error: idf.py not found - activate your esp-idf environment."; exit 1; }
	$(MAKE) -C espidf/tamp stage
	@# Per-variant SDKCONFIG: the default (project-dir sdkconfig) is shared across
	@# build dirs, which would make the opt/noopt toggle silently ineffective.
	idf.py -C devices/espidf -B $(ESP32_BUILD_DIR) -DIDF_TARGET=$(ESP32_TARGET) \
		-D SDKCONFIG=$(ESP32_BUILD_NAME)/sdkconfig $(ESP32_SDKCONFIG_ARG) build

esp32-device-test: esp32-device-build
	@[ -n "$(ESP32_PORT)" ] || { echo "Error: ESP32_PORT is not set (e.g. ESP32_PORT=/dev/ttyUSB0)."; exit 1; }
	idf.py -C devices/espidf -B $(ESP32_BUILD_DIR) -p $(ESP32_PORT) flash
	uv run python tools/device-runner.py --port $(ESP32_PORT)

esp32-device-benchmark: esp32-device-build
	@[ -n "$(ESP32_PORT)" ] || { echo "Error: ESP32_PORT is not set (e.g. ESP32_PORT=/dev/ttyUSB0)."; exit 1; }
	idf.py -C devices/espidf -B $(ESP32_BUILD_DIR) -p $(ESP32_PORT) flash
	uv run python tools/device-runner.py --port $(ESP32_PORT) --benchmark

DEVICE_HELP_TARGETS += esp32-device-help
esp32-device-help:
	@echo "ESP32 on-device harness (requires esp-idf environment):"
	@echo "  make esp32-device-data       Stage embedded data (enwik8 blocks + packed vectors)"
	@echo "  make esp32-device-build      Build the IDF harness (ESP32_TARGET=esp32s3, TAMP_ESP32_OPT=y)"
	@echo "  make esp32-device-test       Build, flash, and run the harness (needs ESP32_PORT)"
	@echo "  make esp32-device-benchmark  Same as test, plus a BENCH summary (needs ESP32_PORT)"
	@echo ""
