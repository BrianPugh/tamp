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
	@echo "On-device benchmarks (requires MPY_DIR and connected device):"
	@echo "  make on-device-compression-benchmark    Run compression benchmark"
	@echo "  make on-device-decompression-benchmark  Run decompression benchmark"
	@echo ""
	@echo "Other targets:"
	@echo "  make download-enwik8   Download enwik8 test dataset"
	@echo "  make tamp-c-library    Build static C library"
	@echo "  make website-build     Build website for deployment"


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
.PHONY: on-device-compression-benchmark on-device-decompression-benchmark mpy-size mpy-compression-benchmark

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

mpy-size:
	@if [ -n "$(MPY_DIR)" ] && [ -x "$(MPY_DIR)/mpy-cross/build/mpy-cross" ]; then \
		MPY_CROSS="$(MPY_DIR)/mpy-cross/build/mpy-cross"; \
	elif command -v mpy-cross >/dev/null 2>&1; then \
		MPY_CROSS="mpy-cross"; \
	else \
		echo "Error: mpy-cross not found. Either set MPY_DIR or install mpy-cross."; \
		exit 1; \
	fi; \
	$$MPY_CROSS -O3 -march=armv6m tamp/__init__.py; \
	size_init=$$(cat tamp/__init__.mpy | wc -c); \
	$$MPY_CROSS -O3 -march=armv6m tamp/compressor_viper.py; \
	size_co_viper=$$(cat tamp/compressor_viper.mpy | wc -c); \
	$$MPY_CROSS -O3 -march=armv6m tamp/decompressor_viper.py; \
	size_de_viper=$$(cat tamp/decompressor_viper.mpy | wc -c); \
	total_viper=$$((size_init + size_co_viper)); \
	total_de_viper=$$((size_init + size_de_viper)); \
	total_all=$$((size_init + size_co_viper + size_de_viper)); \
	printf '__init__.py\t\t\t\t\t\t%s\n' $$size_init; \
	printf 'compressor_viper.py\t\t\t\t\t%s\n' $$size_co_viper; \
	printf 'decompressor_viper.py\t\t\t\t\t%s\n' $$size_de_viper; \
	printf '__init__ + compressor_viper\t\t\t\t%s\n' $$total_viper; \
	printf '__init__ + decompressor_viper\t\t\t\t%s\n' $$total_de_viper; \
	printf '__init__ + compressor_viper + decompressor_viper\t%s\n' $$total_all

mpy-compression-benchmark:
	@time belay run micropython -X heapsize=300M tools/micropython-compression-benchmark.py


##########
# C Tests
##########
# Unit tests using the Unity framework with AddressSanitizer enabled.
.PHONY: c-test clean-c-test

CTEST_CC = gcc
CTEST_SANITIZER_FLAGS = -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -g -O0
CTEST_CFLAGS = -Ictests/Unity/src -Itamp/_c_src $(CTEST_SANITIZER_FLAGS)
CTEST_LDFLAGS = $(CTEST_SANITIZER_FLAGS)

# Tamp library objects for testing
CTEST_TAMP_OBJS = \
	build/ctests/common.o \
	build/ctests/compressor.o \
	build/ctests/decompressor.o

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

# Build test runner (includes test files via #include)
build/ctests/test_runner.o: ctests/test_runner.c ctests/test_compressor.c ctests/test_decompressor.c
	@mkdir -p build/ctests
	$(CTEST_CC) $(CTEST_CFLAGS) -c $< -o $@

# Link test executable
build/test_runner: $(CTEST_TAMP_OBJS) $(CTEST_TEST_OBJS)
	$(CTEST_CC) $(CTEST_LDFLAGS) -o $@ $^

c-test: build/test_runner
	./build/test_runner

clean-c-test:
	@rm -f build/test_runner
	@rm -f build/ctests/*.o
	@rm -f build/unity/*.o


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
