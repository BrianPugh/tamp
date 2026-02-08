# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository.

## Project Overview

Tamp is a low-memory, DEFLATE-inspired lossless compression library designed for
embedded targets. The project contains multiple implementations targeting
different platforms:

### Core Architecture

**Multi-language Implementation Strategy:**

- **C Library** (`tamp/_c_src/tamp/`) - Core implementation with
  compressor/decompressor
- **Python Bindings** - Multiple implementations:
  - Pure Python reference (`tamp/compressor.py`, `tamp/decompressor.py`)
  - Cython-accelerated C bindings (`tamp/_c_*.pyx`) - primary distribution
  - MicroPython Viper (`tamp/*_viper.py`)
  - MicroPython Native Module (`mpy_bindings/`)
- **WebAssembly** (`wasm/`) - JavaScript/TypeScript bindings via Emscripten
- **ESP-IDF Component** (`espidf/`) - ESP32 optimized version

**Shared C Source:** All implementations use the same C source code in
`tamp/_c_src/tamp/`:

- `common.h/c` - Shared utilities, data structures, stream I/O callbacks, and
  dictionary initialization
- `compressor.h/c` - Compression implementation (sink/poll low-level API and
  higher-level compress/flush API)
- `decompressor.h/c` - Decompression implementation
- `compressor_find_match_desktop.c` - Desktop-optimized match finding (included
  by `compressor.c` on non-embedded targets)

## Development Commands

### Python Development

**Environment Setup:**

```bash
poetry install              # Install dependencies
poetry shell               # Activate virtual environment
```

**Build and Test:**

```bash
# Build Cython extensions
poetry run python build.py build_ext --inplace

# Run tests
poetry run pytest                    # All tests
poetry run pytest tests/test_compressor.py  # Specific test file

# Run both Python and MicroPython tests
make test

# CLI usage
poetry run tamp compress input.txt -o output.tamp
poetry run tamp decompress output.tamp -o restored.txt
```

**Code Quality:**

```bash
poetry run ruff check              # Linting
poetry run ruff format             # Formatting
poetry run pyright                 # Type checking
```

**Testing with AddressSanitizer (Linux only):**

```bash
# Build with sanitizers enabled
TAMP_SANITIZE=1 poetry run python build.py build_ext --inplace

# Run tests with AddressSanitizer (requires LD_PRELOAD on Linux)
LD_PRELOAD=$(gcc -print-file-name=libasan.so) \
ASAN_OPTIONS=detect_leaks=0 \
UBSAN_OPTIONS=print_stacktrace=1 \
poetry run pytest

# Note: AddressSanitizer is only supported on Linux due to security
# restrictions on macOS that prevent LD_PRELOAD/DYLD_INSERT_LIBRARIES
```

### WebAssembly Development

**Prerequisites:** Emscripten SDK must be installed and activated

```bash
cd wasm/
activate emsdk              # Activate Emscripten environment
```

**Build and Test:**

```bash
npm run build              # Production build (make all && npm run build:js)
npm run build:debug       # Debug build with symbols
npm test                   # All tests using Node.js test runner
npm run test:basic         # Core functionality tests
npm run test:integration   # API integration tests
```

**Code Quality:**

```bash
npm run format-and-lint    # Prettier + ESLint
npm run type-check         # TypeScript checking
```

### MicroPython Development

**Native Module Build:**

```bash
# Requires MPY_DIR environment variable pointing to MicroPython source
make ARCH=armv6m           # Build for specific architecture
```

**Testing on Device:**

```bash
make on-device-compression-benchmark    # Requires BELAY_DEVICE env var
make on-device-decompression-benchmark
```

### C Library Development

**Build Static Library:**

```bash
make tamp-c-library        # Creates build/tamp.a
```

**Run C Tests:**

```bash
make c-test               # Unit tests using Unity framework
```

### Website Development

**Build and Serve Website:**

```bash
make website-build         # Build website for deployment to build/pages-deploy/
make website-serve         # Build and serve website locally at http://localhost:8000
make website-clean         # Clean website build artifacts
```

## Key Implementation Details

### Build System Architecture

**Python Build Process:**

1. `pyproject.toml` defines Poetry configuration with dynamic versioning
2. `build.py` handles Cython extension compilation with optimization flags
3. Extensions link against shared C source in `tamp/_c_src/tamp/`

**WebAssembly Build Process:**

1. `wasm/Makefile` compiles C source to WebAssembly using Emscripten
2. `tsup` (via `npm run build:js`) bundles into multiple JS/TS distribution
   formats (CJS, ESM, `.d.ts`)
3. Exports specific C functions and runtime methods for JS interop

**Configuration Flags (compile-time `-D` defines):**

- `TAMP_LAZY_MATCHING=1` - Enable lazy matching optimization (default in
  build.py)
- `TAMP_ESP32=1` - ESP32-specific optimizations (avoids bitfields for speed)
- `TAMP_COMPRESSOR`/`TAMP_DECOMPRESSOR` - Include/exclude components
- `TAMP_EXTENDED=1` - Master switch for extended format: RLE and extended match
  (default: 1). `TAMP_EXTENDED_COMPRESS` and `TAMP_EXTENDED_DECOMPRESS` can
  individually override.
- `TAMP_STREAM=1` - Include stream API (default: 1). Disable with
  `-DTAMP_STREAM=0` to save ~2.8KB.
- `TAMP_STREAM_WORK_BUFFER_SIZE=32` - Stack-allocated work buffer for stream API
  (default: 32 bytes, 256+ recommended for performance)
- `TAMP_STREAM_MEMORY` / `TAMP_STREAM_STDIO` / `TAMP_STREAM_LITTLEFS` /
  `TAMP_STREAM_FATFS` - Enable built-in I/O handlers for specific backends
- `TAMP_USE_EMBEDDED_MATCH=1` - Force embedded `find_best_match` implementation
  on desktop (for testing)

**Build Environment Variables (Python):**

- `TAMP_SANITIZE=1` - Enable AddressSanitizer + UBSan
- `TAMP_PROFILE=1` - Enable profiling (line trace, debug info)
- `TAMP_USE_EMBEDDED_MATCH=1` - Force embedded match finding
- `TAMP_BUILD_C_EXTENSIONS=0` - Skip building C extensions entirely
- `CIBUILDWHEEL=1` - CI wheel building mode (disables allowed_to_fail)

### Testing Strategy

**Multi-layered Testing:**

- **Python tests** (`tests/`) - Core algorithm testing using pytest. Includes
  bit reader/writer, compressor, decompressor, round-trip, CLI, dataset
  regression, and file interface tests.
- **WebAssembly tests** (`wasm/test/`) - JS/TS API testing with Node.js test
  runner (`node --test`)
- **C tests** (`ctests/`) - Low-level C API testing using Unity framework
  (submodule at `ctests/Unity/`). Includes stream API tests and filesystem
  integration tests with LittleFS and FatFS RAM backends.
- **Integration tests** - Cross-platform compatibility and performance
  benchmarks

**Test Data Sources:**

- Enwik8 dataset (100MB) for performance benchmarking (`make download-enwik8`)
- Silesia corpus for compression ratio evaluation (`make download-silesia`)
- Custom test cases for edge conditions

### Compressor Architecture

The C compressor uses a two-phase low-level API:

1. `tamp_compressor_sink()` - Copies input bytes into a 16-byte internal ring
   buffer (cheap/fast)
2. `tamp_compressor_poll()` - Runs one compression iteration on the internal
   buffer (computationally intensive)

Higher-level convenience functions (`tamp_compressor_compress`,
`tamp_compressor_compress_and_flush`) wrap these. Callback variants (`_cb`
suffix) accept a `tamp_callback_t` progress callback.

The stream API (`tamp_compress_stream`, `tamp_decompress_stream`) provides a
file-oriented interface using read/write callbacks, supporting multiple I/O
backends (memory, stdio, LittleFS, FatFS).

### Memory Management Patterns

**Key Principle:** Fixed memory usage during compression/decompression

- Window size determines memory usage: `(1 << windowBits)` bytes
- No dynamic allocation during compression/decompression operations
- Stream API uses a stack-allocated work buffer (`TAMP_STREAM_WORK_BUFFER_SIZE`)
- Streaming interfaces require explicit resource management (`destroy()` calls
  in JS/TS)

## Development Workflow

### Making Changes to Core Algorithm

1. **Modify C source** in `tamp/_c_src/tamp/`
2. **Update pure Python reference** in `tamp/compressor.py` /
   `tamp/decompressor.py` to match
3. **Rebuild all implementations:**

   ```bash
   # Python
   poetry run python build.py build_ext --inplace

   # WebAssembly
   cd wasm && npm run build
   ```

4. **Run comprehensive tests:**
   ```bash
   poetry run pytest      # Python tests
   make c-test            # C unit tests with sanitizers
   make c-test-embedded   # C tests with embedded match finding
   cd wasm && npm test    # WebAssembly
   ```

### Adding New Features

1. **Start with C implementation** - All other bindings depend on it
2. **Update exported functions** in WebAssembly Makefile if needed
3. **Add Python bindings** via Cython `.pyx` files
4. **Update JavaScript wrapper** in `wasm/src/`
5. **Add tests** for all implementations

### Performance Optimization

- **Use provided benchmarking tools:**
  ```bash
  make on-device-compression-benchmark     # MicroPython performance
  cd wasm && npm run test:enwik8          # WebAssembly performance
  bash tools/performance-benchmark.sh     # Python performance
  make c-benchmark-stream                 # C stream API benchmark
  make binary-size                        # ARM binary size table
  ```
- **Profile with:** `tools/profiler.py` for Python (requires `TAMP_PROFILE=1`),
  browser dev tools for WebAssembly

### Release Process

1. **Version is automatically managed** via `poetry-dynamic-versioning`
2. **Build artifacts include:**
   - Python wheels with compiled extensions
   - MicroPython `.mpy` files for multiple architectures
   - WebAssembly npm package
3. **CI/CD handles** cross-platform builds and testing

### Python Import Fallback Chain

`tamp/__init__.py` imports Compressor/Decompressor using this priority:

1. Viper (MicroPython optimized) - only available on MicroPython
2. Cython C extensions (`_c_compressor`/`_c_decompressor`) - primary on CPython
3. Pure Python reference (`compressor.py`/`decompressor.py`) - fallback

When modifying compression behavior, changes to the C source must be mirrored in
the pure Python reference implementation to keep them in sync.

### CI/CD

GitHub Actions workflows (`.github/workflows/`):

- `tests.yaml` - Lint (ruff, pre-commit) and test across Python 3.9/3.12/3.13
  and multiple OS. Also runs `c-test` and `c-test-embedded`.
- `build_wheels.yaml` - Cross-platform wheel builds via cibuildwheel
- `javascript.yaml` - WebAssembly tests on Node 18/20
- `mpy_native_module.yaml` - MicroPython native module builds for ARM
  architectures
- `esp_upload_component.yml` - ESP-IDF component registry upload

## Documentation Style

- Avoid "fake" subsections (e.g., bold text like `**Error Promotion:**` acting
  as a heading). Either use a real RST section heading or integrate the content
  into the surrounding prose.
- Keep documentation terse and direct.

## Additional Notes

- **Javascript library is located at `wasm/src/tamp.js`. Do not directly edit
  code in `dist/` or `build/` directories**
