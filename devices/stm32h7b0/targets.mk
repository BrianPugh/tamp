# Root-Makefile targets for the STM32H7B0 harness (included from the repo
# root; run all targets from there). Bare-metal benchmark in
# devices/stm32h7b0/, flashed and run over an ST-Link with OpenOCD
# (semihosting console, no serial port needed). The 128K internal flash cannot
# embed the benchmark data, so run.tcl loads a packed blob into AXI SRAM
# before resuming.
.PHONY: stm32h7b0-device-build stm32h7b0-device-test stm32h7b0-device-benchmark stm32h7b0-device-help

STM32H7B0_OPENOCD := openocd -f interface/stlink.cfg -f target/stm32h7x.cfg -f devices/stm32h7b0/run.tcl

build/stm32h7b0-bench-data.bin: build/enwik8-100kb build/enwik8-100kb-v1.tamp device-vectors
	uv run python tools/pack-device-blob.py build/enwik8-100kb build/enwik8-100kb-v1.tamp \
		build/device-vectors.bin -o build/stm32h7b0-bench-data.bin

stm32h7b0-device-build: build/stm32h7b0-bench-data.bin
	@command -v arm-none-eabi-gcc >/dev/null 2>&1 || { echo "Error: arm-none-eabi-gcc not found."; exit 1; }
	$(MAKE) -C devices/stm32h7b0

stm32h7b0-device-test: stm32h7b0-device-build
	uv run python tools/device-runner.py --exec "$(STM32H7B0_OPENOCD)"

stm32h7b0-device-benchmark: stm32h7b0-device-build
	uv run python tools/device-runner.py --exec "$(STM32H7B0_OPENOCD)" --benchmark

DEVICE_HELP_TARGETS += stm32h7b0-device-help
stm32h7b0-device-help:
	@echo "STM32H7B0 on-device benchmark (requires arm-none-eabi-gcc + openocd + ST-Link):"
	@echo "  make stm32h7b0-device-build      Build the bare-metal firmware"
	@echo "  make stm32h7b0-device-test       Flash over ST-Link and run; nonzero exit on failure"
	@echo "  make stm32h7b0-device-benchmark  Same as test, plus a BENCH summary"
	@echo ""
