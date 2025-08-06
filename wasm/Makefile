# Tamp WebAssembly Build
# Requires Emscripten SDK to be installed and activated

EMCC = emcc
SRCDIR = ../tamp/_c_src/tamp
OUTDIR = dist

# Source files
SOURCES = $(SRCDIR)/common.c $(SRCDIR)/compressor.c $(SRCDIR)/decompressor.c

# Output files
WASM_OUT = $(OUTDIR)/tamp-module.js
MODULE_OUT = $(OUTDIR)/tamp-module.mjs

# Emscripten flags
EMCC_FLAGS = -O3 \
	--no-entry \
	-s WASM=1 \
	-s ALLOW_MEMORY_GROWTH=1 \
	-s EXPORTED_FUNCTIONS='["_malloc", "_free", "_tamp_compressor_init", "_tamp_compressor_flush", "_tamp_compressor_sink", "_tamp_compressor_poll", "_tamp_compressor_full", "_tamp_decompressor_init", "_tamp_decompressor_decompress_cb", "_tamp_decompressor_read_header", "_tamp_compute_min_pattern_size", "_tamp_initialize_dictionary"]' \
	-s EXPORTED_RUNTIME_METHODS='["ccall", "cwrap", "getValue", "setValue", "UTF8ToString", "stringToUTF8", "addFunction", "removeFunction"]' \
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

$(WASM_OUT): $(SOURCES) | $(OUTDIR)
	$(EMCC) $(EMCC_FLAGS) $(SOURCES) -o $@

$(MODULE_OUT): $(SOURCES) | $(OUTDIR)
	$(EMCC) $(EMCC_FLAGS) -s EXPORT_ES6=1 $(SOURCES) -o $@

clean:
	rm -rf $(OUTDIR)

install-deps:
	@echo "Please install Emscripten SDK:"
	@echo "git clone https://github.com/emscripten-core/emsdk.git"
	@echo "cd emsdk && ./emsdk install latest && ./emsdk activate latest"
	@echo "source ./emsdk_env.sh"

test: all
	node test.js

.SECONDARY:
