# Bare-metal STM32H7B0 benchmark firmware. No HAL/Cube: register definitions
# come from ST's BSD-licensed CMSIS headers, fetched (pinned) on first build.
# Invoke via the repo-root Makefile (stm32h7b0-device-*), which stages the
# benchmark data blob this firmware expects in AXI SRAM.

BUILD := build
CMSIS := $(BUILD)/cmsis

CMSIS_DEVICE_H7_TAG := v1.10.7
CMSIS_5_TAG := 5.9.0
CMSIS_DEVICE_URL := https://raw.githubusercontent.com/STMicroelectronics/cmsis_device_h7/$(CMSIS_DEVICE_H7_TAG)/Include
CMSIS_CORE_URL := https://raw.githubusercontent.com/ARM-software/CMSIS_5/$(CMSIS_5_TAG)/CMSIS/Core/Include

CMSIS_HEADERS := \
	$(CMSIS)/stm32h7b0xx.h \
	$(CMSIS)/stm32h7xx.h \
	$(CMSIS)/system_stm32h7xx.h \
	$(CMSIS)/core_cm7.h \
	$(CMSIS)/cmsis_compiler.h \
	$(CMSIS)/cmsis_gcc.h \
	$(CMSIS)/cmsis_version.h \
	$(CMSIS)/mpu_armv7.h \
	$(CMSIS)/cachel1_armv7.h

CC := arm-none-eabi-gcc
TAMP_SRC := ../../tamp/_c_src

# EXTRA_CFLAGS: pass experiment defines from the command line, e.g.
#   make stm32h7b0-device-benchmark EXTRA_CFLAGS=-DTAMP_USE_DESKTOP_MATCH=1
CFLAGS := -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16 \
	-O3 -ggdb -ffunction-sections -fdata-sections \
	-Wall -Wextra \
	-DSTM32H7B0xx \
	-I$(CMSIS) -I. -I../common -I$(TAMP_SRC) \
	$(EXTRA_CFLAGS)

# Full newlib (not nano): the harness prints %llu timings, which newlib-nano's
# printf does not support. 128K of flash has plenty of room.
LDFLAGS := -T linker.ld -nostartfiles \
	-Wl,--gc-sections -Wl,-Map=$(BUILD)/tamp_benchmark.map

SRCS := startup.c system.c syscalls.c main.c \
	../common/tamp_bench.c \
	$(TAMP_SRC)/tamp/common.c \
	$(TAMP_SRC)/tamp/compressor.c \
	$(TAMP_SRC)/tamp/decompressor.c

# #include'd by compressor.c, not compiled separately - deps only.
MATCH_VARIANTS := $(wildcard $(TAMP_SRC)/tamp/compressor_find_match_*.c)

ELF := $(BUILD)/tamp_benchmark.elf

all: $(ELF)

$(CMSIS)/stm32h7b0xx.h $(CMSIS)/stm32h7xx.h $(CMSIS)/system_stm32h7xx.h:
	@mkdir -p $(CMSIS)
	curl -fsSL $(CMSIS_DEVICE_URL)/$(notdir $@) -o $@

$(CMSIS)/core_cm7.h $(CMSIS)/cmsis_compiler.h $(CMSIS)/cmsis_gcc.h $(CMSIS)/cmsis_version.h $(CMSIS)/mpu_armv7.h $(CMSIS)/cachel1_armv7.h:
	@mkdir -p $(CMSIS)
	curl -fsSL $(CMSIS_CORE_URL)/$(notdir $@) -o $@

# Rebuild when the flags change (the whole firmware is one compile+link, so
# there are no per-object dependencies to catch this).
FLAGSTAMP := $(BUILD)/cflags.stamp
$(FLAGSTAMP): force
	@mkdir -p $(BUILD)
	@echo '$(CFLAGS)' | cmp -s - $(FLAGSTAMP) || echo '$(CFLAGS)' > $(FLAGSTAMP)

$(ELF): $(SRCS) $(MATCH_VARIANTS) linker.ld $(CMSIS_HEADERS) Makefile $(FLAGSTAMP)
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $@
	arm-none-eabi-size $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean force
