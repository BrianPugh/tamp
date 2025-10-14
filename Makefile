.PHONY: clean test collect-data venv


ifdef MPY_DIR

# Native machine code in .mpy files
# User can define architecture in call like "make ARCH=armv6m"
# Options:
#     * x86
#     * x64
#     * armv6m
#     * armv7m
#     * armv7emsp
#     * armv7emdp
#     * xtensa
#     * xtensawin
ARCH ?= x64
MOD = tamp

TAMP_COMPRESSOR ?= 1  # Include Tamp compressor in build
TAMP_DECOMPRESSOR ?= 1  # Include Tamp decompressor in build

CFLAGS += -Itamp/_c_src -DTAMP_COMPRESSOR=${TAMP_COMPRESSOR} -DTAMP_DECOMPRESSOR=${TAMP_DECOMPRESSOR}
ifneq ($(CC),clang)
CFLAGS += -fno-tree-loop-distribute-patterns
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
endif

venv:
	@. .venv/bin/activate

clean-cython:
	@rm -rf tamp/*.so
	@rm -rf tamp/_c_compressor.c
	@rm -rf tamp/_c_decompressor.c
	@rm -rf tamp/_c_common.c

clean: clean-cython
	@rm -rf build
	@rm -rf dist

build/enwik8.zip:
	if [ ! -f build/enwik8.zip ]; then \
		mkdir -p build; \
		cd build; \
		curl -O https://mattmahoney.net/dc/enwik8.zip; \
		cd ..; \
	fi

download-enwik8-zip: build/enwik8.zip

build/enwik8: build/enwik8.zip
	if [ ! -f build/enwik8 ]; then \
		cd build; \
		unzip -q enwik8.zip; \
		cd ..; \
	fi

download-enwik8: build/enwik8

build/silesia:
	if [ ! -f build/silesia ]; then \
		mkdir -p build; \
		cd build; \
		curl -O http://mattmahoney.net/dc/silesia.zip; \
		mkdir silesia; \
		unzip -q silesia.zip -d silesia; \
		rm silesia.zip; \
		cd ..; \
	fi

download-silesia: build/silesia

build/enwik8-100kb: download-enwik8
	@head -c 100000 build/enwik8 > build/enwik8-100kb

build/enwik8-100kb.tamp: build/enwik8-100kb
	@uv run tamp compress build/enwik8-100kb -o build/enwik8-100kb.tamp

test: venv
	@uv run python build.py build_ext --inplace && python -m pytest
	@uv run belay run micropython -m unittest tests/*.py
	@echo "All Tests Passed!"

collect-data: venv download-enwik8
	@python tools/collect-data.py 8
	@python tools/collect-data.py 9
	@python tools/collect-data.py 10

on-device-compression-benchmark: venv build/enwik8-100kb build/enwik8-100kb.tamp
	@port=$$(uv run python -c "import os, belay; print(belay.UsbSpecifier.parse_raw(os.environ['BELAY_DEVICE']).to_port())"); \
	echo "Using port: $$port"; \
	uv run mpremote connect port:$$port rm :enwik8-100kb.tamp || true; \
	uv run belay sync '$(BELAY_DEVICE)' build/enwik8-100kb; \
	uv run belay install '$(BELAY_DEVICE)' --with=dev --run tools/on-device-compression-benchmark.py; \
	uv run mpremote connect port:$$port cp :enwik8-100kb.tamp build/on-device-enwik8-100kb.tamp; \
	cmp build/enwik8-100kb.tamp build/on-device-enwik8-100kb.tamp; \
	echo "Success!"

on-device-decompression-benchmark: venv build/enwik8-100kb.tamp
	@port=$$(uv run python -c "import os, belay; print(belay.UsbSpecifier.parse_raw(os.environ['BELAY_DEVICE']).to_port())"); \
	echo "Using port: $$port"; \
	uv run mpremote connect port:$$port rm :enwik8-100kb-decompressed || true; \
	uv run belay sync '$(BELAY_DEVICE)' build/enwik8-100kb.tamp; \
	uv run belay install '$(BELAY_DEVICE)' --with=dev --run tools/on-device-decompression-benchmark.py; \
	uv run mpremote connect port:$$port cp :enwik8-100kb-decompressed build/on-device-enwik8-100kb-decompressed; \
	cmp build/enwik8-100kb build/on-device-enwik8-100kb-decompressed; \
	echo "Success!"

mpy-size:
	@mpy-cross -O3 -march=armv6m tamp/__init__.py; \
		size_init=$$(cat tamp/__init__.mpy | wc -c); \
	    mpy-cross -O3 -march=armv6m tamp/compressor_viper.py; \
		size_co_viper=$$(cat tamp/compressor_viper.mpy | wc -c); \
	    mpy-cross -O3 -march=armv6m tamp/decompressor_viper.py; \
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

CTEST_CC = gcc

# Always enable sanitizers for c-test
SANITIZER_FLAGS = -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -g -O0
SANITIZER_LDFLAGS = -fsanitize=address -fsanitize=undefined

CTEST_CFLAGS = -Ictests/Unity/src -Itamp/_c_src $(SANITIZER_FLAGS)
CTEST_LDFLAGS = -Lctests/unity $(SANITIZER_LDFLAGS)
CTEST_LIBS = -lunity

mkdir-build:
	mkdir -p build
	mkdir -p build/ctests
	mkdir -p build/unity

TAMP_OBJS = \
	build/common.o \
	build/compressor.o \
	build/decompressor.o

CTEST_OBJS = \
	build/unity/unity.o \
	build/ctests/test_runner.o

build/%.o: tamp/_c_src/tamp/%.c mkdir-build
	$(CTEST_CC) $(CTEST_CFLAGS) -c $< -o $@

build/unity/%.o:: ctests/Unity/src/%.c ctests/Unity/src/%.h
	$(CTEST_CC) $(CTEST_CFLAGS) -c $< -o $@

build/ctests/%.o: ctests/%.c
	$(CTEST_CC) $(CTEST_CFLAGS) -c $< -o $@

build/test_runner: $(TAMP_OBJS) $(CTEST_OBJS)
	$(CTEST_CC) $(CTEST_CFLAGS) $(CTEST_LDFLAGS) -o $@ $^ $(LIBS)

c-test: build/test_runner
	./build/test_runner

clean-c-test:
	rm -rf build/test_runner
	rm -rf build/*.o
	rm -rf build/ctests/*.o
	rm -rf build/unity/*.o

##########
# Website #
##########

# Build the website for deployment
website-build:
	cd website && npm install && npm run build

# Serve website locally for development
website-serve:
	cd website && npm install && npm run serve

# Clean website build artifacts
website-clean:
	rm -rf build/pages-deploy
	rm -rf website/node_modules website/package-lock.json

#############
# C Library #
#############
# This section is primarily to build a library to check for implementation size.
BUILDDIR = build
SRCDIR = tamp/_c_src
LIB_CFLAGS = -Os -Wall -I$(SRCDIR) -ffunction-sections -fdata-sections -m32

SRCS = tamp/_c_src/tamp/common.c tamp/_c_src/tamp/compressor.c tamp/_c_src/tamp/decompressor.c
HEADERS = tamp/_c_src/tamp/decompressor.h tamp/_c_src/tamp/compressor.h tamp/_c_src/tamp/common.h

# Define the object files
OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

# Rule to create the target
build/tamp.a: $(OBJS)
	$(AR) rcs $@ $^

tamp-c-library: build/tamp.a
.PHONY: tamp-c-library

# Rule to create object files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(LIB_CFLAGS) -c $< -o $@
