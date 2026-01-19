# Default target - print help
.PHONY: help
help:
	@echo "Tamp - Low-memory compression library"
	@echo ""
	@echo "Common targets:"
	@echo "  make test              Run Python and MicroPython tests"
	@echo "  make c-test            Run C unit tests"
	@echo "  make clean             Clean all build artifacts"
	@echo ""
	@echo "MicroPython native module:"
	@echo "  make mpy               Build native .mpy for RP2040 (requires MPY_DIR)"
	@echo "  make mpy ARCH=armv7m   Build for different architecture"
	@echo ""
	@echo "On-device benchmarks (requires connected device):"
	@echo "  make on-device-compression-benchmark           Run Tamp compression benchmark (requires MPY_DIR)"
	@echo "  make on-device-decompression-benchmark         Run Tamp decompression benchmark (requires MPY_DIR)"
	@echo "  make on-device-deflate-compression-benchmark   Run MicroPython deflate compression benchmark"
	@echo "  make on-device-deflate-decompression-benchmark Run MicroPython deflate decompression benchmark"
	@echo ""
	@echo "Other targets:"
	@echo "  make binary-size        Show binary sizes for README table"
	@echo "  make c-benchmark-stream Benchmark stream API with various temporary working buffer sizes"
	@echo "  make download-enwik8    Download enwik8 test dataset"
	@echo "  make tamp-c-library     Build static C library"
	@echo "  make website-build      Build website for deployment"


###########################
# MicroPython Native Module
###########################
# Build MicroPython native modules (.mpy files).
#
# Usage: make mpy ARCH=armv6m
#        (requires MPY_DIR environment variable pointing to MicroPython source)
#
# Supported architectures:
#     x86, x64, armv6m, armv7m, armv7emsp, armv7emdp, xtensa, xtensawin
#
# Options:
#     TAMP_COMPRESSOR=0    Exclude compressor from build
#     TAMP_DECOMPRESSOR=0  Exclude decompressor from build
.PHONY: mpy

ARCH ?= armv6m
TAMP_COMPRESSOR ?= 1
TAMP_DECOMPRESSOR ?= 1

mpy:
ifndef MPY_DIR
	$(error MPY_DIR must be set to MicroPython source directory)
endif
# x86/x64 native modules can't be built on macOS (produces Mach-O, not ELF)
ifneq ($(filter $(ARCH),x86 x64),)
ifeq ($(shell uname),Darwin)
	$(error Cannot build $(ARCH) native modules on macOS - use Linux or specify an embedded ARCH (e.g., armv6m, armv7m))
endif
endif
	@$(MAKE) --no-print-directory _mpy-build \
		ARCH=$(ARCH) \
		TAMP_COMPRESSOR=$(TAMP_COMPRESSOR) \
		TAMP_DECOMPRESSOR=$(TAMP_DECOMPRESSOR)

# Internal: Only include dynruntime.mk when explicitly building MicroPython modules.
# This prevents accidental builds if MPY_DIR happens to be set in the environment.
ifeq ($(MAKECMDGOALS),_mpy-build)
ifdef MPY_DIR

MOD = tamp

# Override -Os with -O2 for better performance (last flag wins)
CFLAGS_EXTRA = -O2

CFLAGS += -Itamp/_c_src -DTAMP_COMPRESSOR=$(TAMP_COMPRESSOR) -DTAMP_DECOMPRESSOR=$(TAMP_DECOMPRESSOR)
# Compiler-specific flags based on target architecture
ifeq ($(filter $(ARCH),x86 x64),)
# Cross-compiling for embedded (ARM, xtensa) - use GCC flags
CFLAGS += -fno-tree-loop-distribute-patterns
else
# Native x86/x64 - check if using clang (macOS)
ifneq ($(shell $(CC) --version 2>&1 | grep -q clang && echo clang),)
CFLAGS += -Wno-typedef-redefinition
else
CFLAGS += -fno-tree-loop-distribute-patterns
endif
endif

SRC = tamp/_c_src/tamp/common.c mpy_bindings/bindings.c mpy_bindings/bindings_common.py

ifeq ($(strip $(TAMP_COMPRESSOR)),1)
SRC += mpy_bindings/bindings_compressor.py tamp/_c_src/tamp/compressor.c
endif

ifeq ($(strip $(TAMP_DECOMPRESSOR)),1)
SRC += mpy_bindings/bindings_decompressor.py tamp/_c_src/tamp/decompressor.c
endif

MPY_CROSS_FLAGS = -s $(subst compressor,c,$(subst decompressor,d,$(subst bindings,m,$(notdir $<))))

include $(MPY_DIR)/py/dynruntime.mk

# _mpy-build delegates to 'all' which is defined by dynruntime.mk
.PHONY: _mpy-build
_mpy-build: all

endif  # MPY_DIR
endif  # MAKECMDGOALS == _mpy-build


################
# Common/Utility
################
.PHONY: venv clean-cython

venv:
	@. .venv/bin/activate

clean-cython:
	@rm -rf tamp/*.so
	@rm -rf tamp/_c_compressor.c
	@rm -rf tamp/_c_decompressor.c
	@rm -rf tamp/_c_common.c

# Only define 'clean' when not building MicroPython (dynruntime.mk has its own)
ifneq ($(MAKECMDGOALS),_mpy-build)
.PHONY: clean
clean: clean-cython clean-c-test website-clean
	@rm -rf build
	@rm -rf dist
	@rm -f tamp.mpy
endif


################
# Data Downloads
################
# Datasets are stored in datasets/ to persist across `make clean`
.PHONY: download-enwik8-zip download-enwik8 download-silesia

datasets/enwik8.zip:
	@mkdir -p datasets
	@if [ ! -f datasets/enwik8.zip ]; then \
		curl -o datasets/enwik8.zip https://mattmahoney.net/dc/enwik8.zip; \
	fi

download-enwik8-zip: datasets/enwik8.zip

datasets/enwik8: datasets/enwik8.zip
	@if [ ! -f datasets/enwik8 ]; then \
		cd datasets && unzip -q enwik8.zip; \
	fi

download-enwik8: datasets/enwik8

datasets/silesia:
	@mkdir -p datasets
	@if [ ! -d datasets/silesia ]; then \
		curl -o datasets/silesia.zip http://mattmahoney.net/dc/silesia.zip && \
		mkdir -p datasets/silesia && \
		unzip -q datasets/silesia.zip -d datasets/silesia && \
		rm datasets/silesia.zip; \
	fi

download-silesia: datasets/silesia

# Derived test files (OK to be in build/, quick to regenerate)
build/enwik8-100kb: download-enwik8
	@mkdir -p build
	@head -c 100000 datasets/enwik8 > build/enwik8-100kb

build/enwik8-100kb.tamp: build/enwik8-100kb
	@poetry run tamp compress build/enwik8-100kb -o build/enwik8-100kb.tamp


##################
# Python / Testing
##################
.PHONY: test collect-data

test: venv
	@poetry run python build.py build_ext --inplace && python -m pytest
	@poetry run belay run micropython -m unittest tests/*.py
	@echo "All Tests Passed!"

collect-data: venv download-enwik8
	@python tools/collect-data.py 8
	@python tools/collect-data.py 9
	@python tools/collect-data.py 10


#######################
# MicroPython Utilities
#######################
.PHONY: on-device-compression-benchmark on-device-decompression-benchmark on-device-deflate-compression-benchmark on-device-deflate-decompression-benchmark mpy-viper-size mpy-native-size mpy-compression-benchmark

MPREMOTE := poetry run mpremote

# Helper to sync a file only if hash differs: $(call mpremote-sync,local,remote)
define mpremote-sync
	@local_hash=$$(shasum -a 256 $(1) | cut -d' ' -f1); \
	remote_hash=$$($(MPREMOTE) sha256sum :$(2) 2>/dev/null | tail -1 || echo ""); \
	if [ "$$local_hash" != "$$remote_hash" ]; then \
		echo "Syncing $(1) -> :$(2)"; \
		$(MPREMOTE) cp $(1) :$(2); \
	else \
		echo "Skipping $(1) (unchanged)"; \
	fi
endef

on-device-compression-benchmark: mpy build/enwik8-100kb build/enwik8-100kb.tamp
	$(MPREMOTE) rm :enwik8-100kb.tamp || true
	@# Remove any viper implementation that may exist from previous belay syncs
	$(MPREMOTE) rm :tamp/__init__.py :tamp/compressor_viper.py :tamp/decompressor_viper.py :tamp/compressor.py :tamp/decompressor.py :tamp/__main__.py :tamp/py.typed 2>/dev/null || true
	$(MPREMOTE) rmdir :tamp 2>/dev/null || true
	$(MPREMOTE) mkdir :lib || true
	$(call mpremote-sync,tamp.mpy,lib/tamp.mpy)
	$(call mpremote-sync,build/enwik8-100kb,enwik8-100kb)
	$(MPREMOTE) soft-reset
	$(MPREMOTE) run tools/on-device-compression-benchmark.py
	$(MPREMOTE) cp :enwik8-100kb.tamp build/on-device-enwik8-100kb.tamp
	cmp build/enwik8-100kb.tamp build/on-device-enwik8-100kb.tamp
	@echo "Success!"

on-device-decompression-benchmark: mpy build/enwik8-100kb.tamp
	$(MPREMOTE) rm :enwik8-100kb-decompressed || true
	@# Remove any viper implementation that may exist from previous belay syncs
	$(MPREMOTE) rm :tamp/__init__.py :tamp/compressor_viper.py :tamp/decompressor_viper.py :tamp/compressor.py :tamp/decompressor.py :tamp/__main__.py :tamp/py.typed 2>/dev/null || true
	$(MPREMOTE) rmdir :tamp 2>/dev/null || true
	$(MPREMOTE) mkdir :lib || true
	$(call mpremote-sync,tamp.mpy,lib/tamp.mpy)
	$(call mpremote-sync,build/enwik8-100kb.tamp,enwik8-100kb.tamp)
	$(MPREMOTE) soft-reset
	$(MPREMOTE) run tools/on-device-decompression-benchmark.py
	$(MPREMOTE) cp :enwik8-100kb-decompressed build/on-device-enwik8-100kb-decompressed
	cmp build/enwik8-100kb build/on-device-enwik8-100kb-decompressed
	@echo "Success!"

on-device-deflate-compression-benchmark: build/enwik8-100kb
	$(MPREMOTE) rm :enwik8-100kb.deflate || true
	$(call mpremote-sync,build/enwik8-100kb,enwik8-100kb)
	$(MPREMOTE) soft-reset
	$(MPREMOTE) run tools/on-device-deflate-compression-benchmark.py
	$(MPREMOTE) cp :enwik8-100kb.deflate build/on-device-enwik8-100kb.deflate
	@echo "Success!"

on-device-deflate-decompression-benchmark: build/enwik8-100kb
	@# First ensure we have the deflate-compressed file on device
	@if ! $(MPREMOTE) sha256sum :enwik8-100kb.deflate >/dev/null 2>&1; then \
		echo "Error: enwik8-100kb.deflate not found on device. Run on-device-deflate-compression-benchmark first."; \
		exit 1; \
	fi
	$(MPREMOTE) rm :enwik8-100kb-decompressed || true
	$(MPREMOTE) soft-reset
	$(MPREMOTE) run tools/on-device-deflate-decompression-benchmark.py
	$(MPREMOTE) cp :enwik8-100kb-decompressed build/on-device-enwik8-100kb-decompressed
	cmp build/enwik8-100kb build/on-device-enwik8-100kb-decompressed
	@echo "Success!"

mpy-viper-size:
	@if [ -n "$(MPY_DIR)" ] && [ -x "$(MPY_DIR)/mpy-cross/build/mpy-cross" ]; then \
		MPY_CROSS="$(MPY_DIR)/mpy-cross/build/mpy-cross"; \
	elif command -v mpy-cross >/dev/null 2>&1; then \
		MPY_CROSS="mpy-cross"; \
	else \
		echo "Error: mpy-cross not found. Either set MPY_DIR or install mpy-cross."; \
		exit 1; \
	fi; \
	$$MPY_CROSS -O3 -march=armv6m -o /tmp/_tamp_init.mpy tamp/__init__.py; \
	$$MPY_CROSS -O3 -march=armv6m -o /tmp/_tamp_comp.mpy tamp/compressor_viper.py; \
	$$MPY_CROSS -O3 -march=armv6m -o /tmp/_tamp_decomp.mpy tamp/decompressor_viper.py; \
	size_init=$$(wc -c < /tmp/_tamp_init.mpy | tr -d ' '); \
	size_comp=$$(wc -c < /tmp/_tamp_comp.mpy | tr -d ' '); \
	size_decomp=$$(wc -c < /tmp/_tamp_decomp.mpy | tr -d ' '); \
	rm -f /tmp/_tamp_init.mpy /tmp/_tamp_comp.mpy /tmp/_tamp_decomp.mpy; \
	printf 'Tamp (MicroPython Viper)   %d  %d  %d\n' \
		$$((size_init + size_comp)) $$((size_init + size_decomp)) $$((size_init + size_comp + size_decomp))

mpy-native-size:
ifndef MPY_DIR
	$(error MPY_DIR must be set for mpy-native-size)
endif
	@rm -rf tamp.mpy build/tamp build/mpy_bindings build/tamp.native.mpy && \
		$(MAKE) -s _mpy-build MPY_DIR=$(MPY_DIR) ARCH=armv6m TAMP_COMPRESSOR=1 TAMP_DECOMPRESSOR=0 >/dev/null 2>&1 && \
		size_comp=$$(wc -c < tamp.mpy | tr -d ' ') && \
		rm -rf tamp.mpy build/tamp build/mpy_bindings build/tamp.native.mpy && \
		$(MAKE) -s _mpy-build MPY_DIR=$(MPY_DIR) ARCH=armv6m TAMP_COMPRESSOR=0 TAMP_DECOMPRESSOR=1 >/dev/null 2>&1 && \
		size_decomp=$$(wc -c < tamp.mpy | tr -d ' ') && \
		rm -rf tamp.mpy build/tamp build/mpy_bindings build/tamp.native.mpy && \
		$(MAKE) -s _mpy-build MPY_DIR=$(MPY_DIR) ARCH=armv6m TAMP_COMPRESSOR=1 TAMP_DECOMPRESSOR=1 >/dev/null 2>&1 && \
		size_both=$$(wc -c < tamp.mpy | tr -d ' ') && \
		printf 'Tamp (MicroPython Native)  %s  %s  %s\n' $$size_comp $$size_decomp $$size_both

mpy-compression-benchmark:
	@time belay run micropython -X heapsize=300M tools/micropython-compression-benchmark.py


##########
# C Tests
##########
# Unit tests using the Unity framework with AddressSanitizer enabled.
.PHONY: c-test clean-c-test

CTEST_CC = gcc
CTEST_SANITIZER_FLAGS = -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -g -O0
CTEST_INCLUDES = -Ictests/Unity/src -Itamp/_c_src -Ictests -Ictests/littlefs -Ictests/fatfs/source
CTEST_DEFINES = -DTAMP_STREAM_STDIO=1 -DTAMP_STREAM_MEMORY=1 \
	-DTAMP_STREAM_LITTLEFS=1 -DTEST_LITTLEFS=1 \
	-DTAMP_STREAM_FATFS=1 -DTEST_FATFS=1 \
	-DLFS_NO_DEBUG -DLFS_NO_WARN -DLFS_NO_ERROR
CTEST_CFLAGS = $(CTEST_INCLUDES) $(CTEST_SANITIZER_FLAGS) $(CTEST_DEFINES)
CTEST_LDFLAGS = $(CTEST_SANITIZER_FLAGS)

# Tamp library objects for testing
CTEST_TAMP_OBJS = \
	build/ctests/common.o \
	build/ctests/compressor.o \
	build/ctests/decompressor.o

# LittleFS objects
CTEST_LFS_OBJS = \
	build/ctests/lfs.o \
	build/ctests/lfs_util.o \
	build/ctests/lfs_rambd.o

# FatFs objects
CTEST_FATFS_OBJS = \
	build/ctests/ff.o \
	build/ctests/ffunicode.o \
	build/ctests/fatfs_ramdisk.o

# Test framework and test runner objects
CTEST_TEST_OBJS = \
	build/unity/unity.o \
	build/ctests/test_runner.o

# Build tamp source files for testing
build/ctests/%.o: tamp/_c_src/tamp/%.c
	@mkdir -p build/ctests
	$(CTEST_CC) $(CTEST_CFLAGS) -c $< -o $@

# Build Unity framework
build/unity/unity.o: ctests/Unity/src/unity.c ctests/Unity/src/unity.h
	@mkdir -p build/unity
	$(CTEST_CC) $(CTEST_CFLAGS) -c $< -o $@

# Build LittleFS
build/ctests/lfs.o: ctests/littlefs/lfs.c
	@mkdir -p build/ctests
	$(CTEST_CC) $(CTEST_CFLAGS) -c $< -o $@

build/ctests/lfs_util.o: ctests/littlefs/lfs_util.c
	@mkdir -p build/ctests
	$(CTEST_CC) $(CTEST_CFLAGS) -c $< -o $@

build/ctests/lfs_rambd.o: ctests/lfs_rambd.c
	@mkdir -p build/ctests
	$(CTEST_CC) $(CTEST_CFLAGS) -c $< -o $@

# Build FatFs
build/ctests/ff.o: ctests/fatfs/source/ff.c
	@mkdir -p build/ctests
	$(CTEST_CC) $(CTEST_CFLAGS)  -c $< -o $@

build/ctests/ffunicode.o: ctests/fatfs/source/ffunicode.c
	@mkdir -p build/ctests
	$(CTEST_CC) $(CTEST_CFLAGS)  -c $< -o $@

build/ctests/fatfs_ramdisk.o: ctests/fatfs_ramdisk.c
	@mkdir -p build/ctests
	$(CTEST_CC) $(CTEST_CFLAGS)  -c $< -o $@

# Build test runner (includes test files via #include)
build/ctests/test_runner.o: ctests/test_runner.c ctests/test_compressor.c ctests/test_decompressor.c ctests/test_stream.c ctests/test_stream_filesystems.c
	@mkdir -p build/ctests
	$(CTEST_CC) $(CTEST_CFLAGS)  -c $< -o $@

# Link test executable
build/test_runner: $(CTEST_TAMP_OBJS) $(CTEST_LFS_OBJS) $(CTEST_FATFS_OBJS) $(CTEST_TEST_OBJS)
	$(CTEST_CC) $(CTEST_LDFLAGS) -o $@ $^

c-test: build/test_runner
	./build/test_runner

clean-c-test:
	@rm -f build/test_runner
	@rm -f build/ctests/*.o
	@rm -f build/unity/*.o


###############
# C Benchmarks
###############
# Benchmark the stream API with different temporary working buffer sizes.
# Uses the enwik8 dataset (100MB) for realistic performance measurements.
.PHONY: c-benchmark-stream

c-benchmark-stream: download-enwik8
	@echo "Stream API Benchmark (datasets/enwik8, 100MB)"
	@echo ""
	@printf "%-20s %-15s %s\n" "Work Buffer Size" "Compress Time" "Decompress Time"
	@printf "%-20s %-15s %s\n" "----------------" "-------------" "---------------"
	@for size in 32 64 128 256 512 1024; do \
		gcc -O3 -DTAMP_STREAM_STDIO=1 -DTAMP_STREAM_WORK_BUFFER_SIZE=$$size \
			-Itamp/_c_src tools/benchmark_stream.c \
			tamp/_c_src/tamp/common.c \
			tamp/_c_src/tamp/compressor.c \
			tamp/_c_src/tamp/decompressor.c \
			-o build/benchmark_stream_$$size; \
		output=$$(./build/benchmark_stream_$$size datasets/enwik8 2>&1); \
		compress_time=$$(echo "$$output" | grep "Compression:" | sed 's/Compression: \([0-9.]*\)s.*/\1/'); \
		decompress_time=$$(echo "$$output" | grep "Decompression:" | sed 's/Decompression: \([0-9.]*\)s.*/\1/'); \
		printf "%-20s %-15s %s\n" "$$size bytes" "$${compress_time}s" "$${decompress_time}s"; \
	done
	@rm -f build/benchmark_stream_*


#############
# C Library
#############
# Build a static library to check implementation size.
# Uses -Os for size optimization and -m32 for 32-bit target.
.PHONY: tamp-c-library

LIB_CC = $(CC)
LIB_CFLAGS = -Os -Wall -Itamp/_c_src -ffunction-sections -fdata-sections -m32

LIB_TAMP_OBJS = \
	build/lib/common.o \
	build/lib/compressor.o \
	build/lib/decompressor.o

build/lib/%.o: tamp/_c_src/tamp/%.c
	@mkdir -p build/lib
	$(LIB_CC) $(LIB_CFLAGS) -c $< -o $@

build/tamp.a: $(LIB_TAMP_OBJS)
	$(AR) rcs $@ $^

tamp-c-library: build/tamp.a


###############
# Binary Sizes
###############
# Generate binary size information for README table (armv6m with -O3).
.PHONY: binary-size c-size

ARM_CC := arm-none-eabi-gcc
ARM_AR := arm-none-eabi-ar
ARM_SIZE := arm-none-eabi-size
ARM_CFLAGS = -O3 -Wall -Itamp/_c_src -mcpu=cortex-m0plus -mthumb -ffunction-sections -fdata-sections

C_SRC_COMMON = tamp/_c_src/tamp/common.c
C_SRC_COMP = tamp/_c_src/tamp/compressor.c
C_SRC_DECOMP = tamp/_c_src/tamp/decompressor.c

# Build compressor-only library (without stream API)
build/arm/tamp_comp.a: $(C_SRC_COMMON) $(C_SRC_COMP)
	@mkdir -p build/arm
	$(ARM_CC) $(ARM_CFLAGS) -DTAMP_COMPRESSOR=1 -DTAMP_DECOMPRESSOR=0 -DTAMP_STREAM=0 -c $(C_SRC_COMMON) -o build/arm/common_c.o
	$(ARM_CC) $(ARM_CFLAGS) -DTAMP_COMPRESSOR=1 -DTAMP_DECOMPRESSOR=0 -DTAMP_STREAM=0 -c $(C_SRC_COMP) -o build/arm/compressor.o
	$(ARM_AR) rcs $@ build/arm/common_c.o build/arm/compressor.o

# Build decompressor-only library (without stream API)
build/arm/tamp_decomp.a: $(C_SRC_COMMON) $(C_SRC_DECOMP)
	@mkdir -p build/arm
	$(ARM_CC) $(ARM_CFLAGS) -DTAMP_COMPRESSOR=0 -DTAMP_DECOMPRESSOR=1 -DTAMP_STREAM=0 -c $(C_SRC_COMMON) -o build/arm/common_d.o
	$(ARM_CC) $(ARM_CFLAGS) -DTAMP_COMPRESSOR=0 -DTAMP_DECOMPRESSOR=1 -DTAMP_STREAM=0 -c $(C_SRC_DECOMP) -o build/arm/decompressor.o
	$(ARM_AR) rcs $@ build/arm/common_d.o build/arm/decompressor.o

# Build full library (without stream API)
build/arm/tamp_full.a: $(C_SRC_COMMON) $(C_SRC_COMP) $(C_SRC_DECOMP)
	@mkdir -p build/arm
	$(ARM_CC) $(ARM_CFLAGS) -DTAMP_COMPRESSOR=1 -DTAMP_DECOMPRESSOR=1 -DTAMP_STREAM=0 -c $(C_SRC_COMMON) -o build/arm/common_f.o
	$(ARM_CC) $(ARM_CFLAGS) -DTAMP_COMPRESSOR=1 -DTAMP_DECOMPRESSOR=1 -DTAMP_STREAM=0 -c $(C_SRC_COMP) -o build/arm/compressor_f.o
	$(ARM_CC) $(ARM_CFLAGS) -DTAMP_COMPRESSOR=1 -DTAMP_DECOMPRESSOR=1 -DTAMP_STREAM=0 -c $(C_SRC_DECOMP) -o build/arm/decompressor_f.o
	$(ARM_AR) rcs $@ build/arm/common_f.o build/arm/compressor_f.o build/arm/decompressor_f.o

# Build compressor-only library (with stream API, the default)
build/arm/tamp_comp_stream.a: $(C_SRC_COMMON) $(C_SRC_COMP)
	@mkdir -p build/arm
	$(ARM_CC) $(ARM_CFLAGS) -DTAMP_COMPRESSOR=1 -DTAMP_DECOMPRESSOR=0 -c $(C_SRC_COMMON) -o build/arm/common_cs.o
	$(ARM_CC) $(ARM_CFLAGS) -DTAMP_COMPRESSOR=1 -DTAMP_DECOMPRESSOR=0 -c $(C_SRC_COMP) -o build/arm/compressor_s.o
	$(ARM_AR) rcs $@ build/arm/common_cs.o build/arm/compressor_s.o

# Build decompressor-only library (with stream API, the default)
build/arm/tamp_decomp_stream.a: $(C_SRC_COMMON) $(C_SRC_DECOMP)
	@mkdir -p build/arm
	$(ARM_CC) $(ARM_CFLAGS) -DTAMP_COMPRESSOR=0 -DTAMP_DECOMPRESSOR=1 -c $(C_SRC_COMMON) -o build/arm/common_ds.o
	$(ARM_CC) $(ARM_CFLAGS) -DTAMP_COMPRESSOR=0 -DTAMP_DECOMPRESSOR=1 -c $(C_SRC_DECOMP) -o build/arm/decompressor_s.o
	$(ARM_AR) rcs $@ build/arm/common_ds.o build/arm/decompressor_s.o

# Build full library (with stream API, the default)
build/arm/tamp_full_stream.a: $(C_SRC_COMMON) $(C_SRC_COMP) $(C_SRC_DECOMP)
	@mkdir -p build/arm
	$(ARM_CC) $(ARM_CFLAGS) -DTAMP_COMPRESSOR=1 -DTAMP_DECOMPRESSOR=1 -c $(C_SRC_COMMON) -o build/arm/common_fs.o
	$(ARM_CC) $(ARM_CFLAGS) -DTAMP_COMPRESSOR=1 -DTAMP_DECOMPRESSOR=1 -c $(C_SRC_COMP) -o build/arm/compressor_fs.o
	$(ARM_CC) $(ARM_CFLAGS) -DTAMP_COMPRESSOR=1 -DTAMP_DECOMPRESSOR=1 -c $(C_SRC_DECOMP) -o build/arm/decompressor_fs.o
	$(ARM_AR) rcs $@ build/arm/common_fs.o build/arm/compressor_fs.o build/arm/decompressor_fs.o

c-size:
	@rm -rf build/arm
	@$(MAKE) --no-print-directory build/arm/tamp_comp_stream.a build/arm/tamp_decomp_stream.a build/arm/tamp_full_stream.a build/arm/tamp_comp.a build/arm/tamp_decomp.a build/arm/tamp_full.a
	@size_comp=$$($(ARM_SIZE) -B --totals build/arm/tamp_comp.a 2>/dev/null | grep TOTALS | awk '{print $$1+$$2}'); \
	size_decomp=$$($(ARM_SIZE) -B --totals build/arm/tamp_decomp.a 2>/dev/null | grep TOTALS | awk '{print $$1+$$2}'); \
	size_full=$$($(ARM_SIZE) -B --totals build/arm/tamp_full.a 2>/dev/null | grep TOTALS | awk '{print $$1+$$2}'); \
	printf 'Tamp (C, -DTAMP_STREAM=0)  %d  %d  %d\n' $$size_comp $$size_decomp $$size_full
	@size_comp=$$($(ARM_SIZE) -B --totals build/arm/tamp_comp_stream.a 2>/dev/null | grep TOTALS | awk '{print $$1+$$2}'); \
	size_decomp=$$($(ARM_SIZE) -B --totals build/arm/tamp_decomp_stream.a 2>/dev/null | grep TOTALS | awk '{print $$1+$$2}'); \
	size_full=$$($(ARM_SIZE) -B --totals build/arm/tamp_full_stream.a 2>/dev/null | grep TOTALS | awk '{print $$1+$$2}'); \
	printf 'Tamp (C)                   %d  %d  %d\n' $$size_comp $$size_decomp $$size_full

binary-size:
	@echo "Binary sizes for armv6m (bytes):"
	@echo ""
	@printf '%-27s %-10s %-12s %s\n' "" "Compressor" "Decompressor" "Compressor + Decompressor"
	@printf '%-27s %-10s %-12s %s\n' "---------------------------" "----------" "------------" "-------------------------"
	@output=$$($(MAKE) -s mpy-viper-size 2>&1) && echo "$$output" || echo "Tamp (MicroPython Viper)   (requires mpy-cross)"
	@output=$$($(MAKE) -s mpy-native-size 2>&1) && echo "$$output" || echo "Tamp (MicroPython Native)  (requires MPY_DIR)"
	@output=$$($(MAKE) -s c-size 2>&1) && echo "$$output" || echo "Tamp (C)                   (requires arm-none-eabi-gcc)"


##########
# Website
##########
.PHONY: website-build website-serve website-clean

website-build:
	cd website && npm install && npm run build

website-serve:
	cd website && npm install && npm run serve

website-clean:
	@rm -rf build/pages-deploy
	@rm -rf website/node_modules
