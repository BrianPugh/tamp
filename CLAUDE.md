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

- `common.h/c` - Shared utilities and data structures
- `compressor.h/c` - Compression implementation
- `decompressor.h/c` - Decompression implementation

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
2. `wasm/scripts/build.js` generates multiple JS/TS distribution formats
3. Exports specific C functions and runtime methods for JS interop

**Configuration Flags:**

- `TAMP_LAZY_MATCHING=1` - Enable lazy matching optimization (default)
- `TAMP_ESP32=1` - ESP32-specific optimizations
- `TAMP_COMPRESSOR`/`TAMP_DECOMPRESSOR` - Include/exclude components

### Testing Strategy

**Multi-layered Testing:**

- **Python tests** (`tests/`) - Core algorithm testing using pytest
- **WebAssembly tests** (`wasm/test/`) - JS/TS API testing with Node.js test
  runner
- **C tests** (`ctests/`) - Low-level C API testing using Unity framework
- **Integration tests** - Cross-platform compatibility and performance
  benchmarks

**Test Data Sources:**

- Enwik8 dataset (100MB) for performance benchmarking
- Silesia corpus for compression ratio evaluation
- Custom test cases for edge conditions

### Memory Management Patterns

**Key Principle:** Fixed memory usage during compression/decompression

- Window size determines memory usage: `(1 << windowBits)` bytes
- No dynamic allocation during compression/decompression operations
- Streaming interfaces require explicit resource management (`destroy()` calls
  in JS/TS)

## Development Workflow

### Making Changes to Core Algorithm

1. **Modify C source** in `tamp/_c_src/tamp/`
2. **Rebuild all implementations:**

   ```bash
   # Python
   poetry run python build.py build_ext --inplace

   # WebAssembly
   cd wasm && npm run build
   ```

3. **Run comprehensive tests:**
   ```bash
   make test              # Python + MicroPython
   cd wasm && npm test    # WebAssembly
   make c-test           # C unit tests
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
  npm run test:enwik8                     # WebAssembly performance
  python tools/performance-benchmark.sh   # Python performance
  ```
- **Profile with:** `tools/profiler.py` for Python, browser dev tools for
  WebAssembly

### Release Process

1. **Version is automatically managed** via `poetry-dynamic-versioning`
2. **Build artifacts include:**
   - Python wheels with compiled extensions
   - MicroPython `.mpy` files for multiple architectures
   - WebAssembly npm package
3. **CI/CD handles** cross-platform builds and testing

## Documentation Style

- Avoid "fake" subsections (e.g., bold text like `**Error Promotion:**` acting
  as a heading). Either use a real RST section heading or integrate the content
  into the surrounding prose.
- Keep documentation terse and direct.

## Additional Notes

- **Javascript library is located at `wasm/src/tamp.js`. Do not directly edit
  code in `dist/` or `build/` directories**
