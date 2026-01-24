# Tamp WebAssembly Build
# Requires Emscripten SDK to be installed and activated

# Check if emcc is available
EMCC := $(shell command -v emcc 2> /dev/null)
ifndef EMCC
    $(info )
    $(info ERROR: emcc not found! Please activate the Emscripten SDK:)
    $(info   source /path/to/emsdk/emsdk_env.sh)
    $(info )
    $(info If you haven't installed the SDK yet:)
    $(info   git clone https://github.com/emscripten-core/emsdk.git)
    $(info   cd emsdk && ./emsdk install latest && ./emsdk activate latest)
    $(info   source ./emsdk_env.sh)
    $(info )
    $(error Emscripten SDK not activated)
endif

EMCC = emcc
SRCDIR = ../tamp/_c_src/tamp
OUTDIR = dist

# Source files (core library)
SOURCES = $(SRCDIR)/common.c $(SRCDIR)/compressor.c $(SRCDIR)/decompressor.c

# WASM-only source files (sizeof functions for JS interop)
WASM_SOURCES = src/sizeof.c

# Output files
WASM_OUT = $(OUTDIR)/tamp-module.js
MODULE_OUT = $(OUTDIR)/tamp-module.mjs

# Emscripten flags
EMCC_FLAGS = -O3 \
	--no-entry \
	-s WASM=1 \
	-s ALLOW_MEMORY_GROWTH=1 \
	-s EXPORTED_FUNCTIONS='["_malloc", "_free", "_tamp_compressor_init", "_tamp_compressor_flush", "_tamp_compressor_sink", "_tamp_compressor_poll", "_tamp_compressor_full", "_tamp_compressor_sizeof", "_tamp_decompressor_init", "_tamp_decompressor_decompress_cb", "_tamp_decompressor_read_header", "_tamp_decompressor_sizeof", "_tamp_compute_min_pattern_size", "_tamp_initialize_dictionary", "_tamp_conf_sizeof"]' \
	-s EXPORTED_RUNTIME_METHODS='["ccall", "cwrap", "getValue", "setValue", "UTF8ToString", "stringToUTF8", "addFunction", "removeFunction", "HEAPU8"]' \
	-s MODULARIZE=1 \
	-s EXPORT_NAME="TampModule" \
	-s ENVIRONMENT='web,webview,worker,node' \
	-s MALLOC=emmalloc \
	-s STACK_SIZE=65536 \
	-s INITIAL_MEMORY=131072 \
	-s RESERVED_FUNCTION_POINTERS=10 \
	-DTAMP_LAZY_MATCHING=1

# Development flags (add debug info)
EMCC_DEBUG_FLAGS = $(EMCC_FLAGS) \
	-g \
	-s ASSERTIONS=1 \
	-s SAFE_HEAP=1

.PHONY: all clean debug

all: $(WASM_OUT) $(MODULE_OUT)

debug: EMCC_FLAGS = $(EMCC_DEBUG_FLAGS)
debug: all

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(WASM_OUT): $(SOURCES) $(WASM_SOURCES) | $(OUTDIR)
	$(EMCC) $(EMCC_FLAGS) $(SOURCES) $(WASM_SOURCES) -o $@

$(MODULE_OUT): $(SOURCES) $(WASM_SOURCES) | $(OUTDIR)
	$(EMCC) $(EMCC_FLAGS) -s EXPORT_ES6=1 $(SOURCES) $(WASM_SOURCES) -o $@

clean:
	rm -rf $(OUTDIR)

install-deps:
	@echo "Please install Emscripten SDK:"
	@echo "git clone https://github.com/emscripten-core/emsdk.git"
	@echo "cd emsdk && ./emsdk install latest && ./emsdk activate latest"
	@echo "source ./emsdk_env.sh"

test: all
	npm test

.SECONDARY:
