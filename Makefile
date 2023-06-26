.PHONY: clean test collect-data venv


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

build/enwik8:
	if [ ! -f build/enwik8 ]; then \
		mkdir -p build; \
		cd build; \
		curl -O https://mattmahoney.net/dc/enwik8.zip; \
		unzip -q enwik8.zip; \
		rm enwik8.zip; \
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
	@poetry run tamp compress build/enwik8-100kb -o build/enwik8-100kb.tamp

test: venv
	@poetry run python build.py build_ext --inplace && python -m pytest
	@poetry run belay run micropython -m unittest tests/*.py
	@echo "All Tests Passed!"

collect-data: venv download-enwik8
	@python tools/collect-data.py 8
	@python tools/collect-data.py 9
	@python tools/collect-data.py 10

on-device-compression-benchmark: venv build/enwik8-100kb build/enwik8-100kb.tamp
	@port=$$(python -c "import os, belay; print(belay.UsbSpecifier.parse_raw(os.environ['BELAY_DEVICE']).to_port())"); \
	echo "Using port: $$port"; \
	mpremote connect port:$$port rm :enwik8-100kb.tamp || true; \
	belay sync '$(BELAY_DEVICE)' build/enwik8-100kb; \
	belay install '$(BELAY_DEVICE)' --with=dev --run tools/on-device-compression-benchmark.py; \
	mpremote connect port:$$port cp :enwik8-100kb.tamp build/on-device-enwik8-100kb.tamp; \
	cmp build/enwik8-100kb.tamp build/on-device-enwik8-100kb.tamp; \
	echo "Success!"

on-device-decompression-benchmark: venv build/enwik8-100kb.tamp
	@port=$$(python -c "import os, belay; print(belay.UsbSpecifier.parse_raw(os.environ['BELAY_DEVICE']).to_port())"); \
	echo "Using port: $$port"; \
	mpremote connect port:$$port rm :enwik8-100kb-decompressed || true; \
	belay sync '$(BELAY_DEVICE)' build/enwik8-100kb.tamp; \
	belay install '$(BELAY_DEVICE)' --with=dev --run tools/on-device-decompression-benchmark.py; \
	mpremote connect port:$$port cp :enwik8-100kb-decompressed build/on-device-enwik8-100kb-decompressed; \
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


#############
# C Library #
#############
# This section is primarily to build a library to check for implementation size.
BUILDDIR = build
SRCDIR = tamp/_c_src
CFLAGS = -Os -Wall -I$(SRCDIR) -ffunction-sections -fdata-sections -m32

# Collect all .c and .h files
SRCS = $(shell find $(SRCDIR) -name "*.c")
HEADERS = $(shell find $(SRCDIR) -name "*.h")

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
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: %.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
