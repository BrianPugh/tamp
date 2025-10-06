# ESP32 QEMU Tests

This directory contains QEMU-based tests for the ESP32 implementation of the
Tamp compression library using **pytest-embedded**, Espressif's official testing
framework.

## Prerequisites

### 1. Python Version

**Python 3.9 or later is recommended** for ESP-IDF 5.5 and pytest-embedded 1.x.

```bash
python3 --version  # Should be 3.9+
```

### 2. ESP-IDF Installation

Install ESP-IDF (version 5.5 or later recommended):

```bash
# Follow official ESP-IDF installation guide
# https://docs.espressif.com/projects/esp-idf/en/latest/get-started/
```

### 3. Install System Dependencies (Linux/macOS only)

Before installing QEMU, install required system libraries:

**Ubuntu/Debian:**

```bash
sudo apt-get install -y libgcrypt20 libglib2.0-0 libpixman-1-0 libsdl2-2.0-0 libslirp0
```

**CentOS:**

```bash
sudo yum install -y --enablerepo=powertools libgcrypt glib2 pixman SDL2 libslirp
```

**Arch:**

```bash
sudo pacman -S --needed libgcrypt glib2 pixman sdl2 libslirp
```

**macOS:**

```bash
brew install libgcrypt glib pixman sdl2 libslirp
```

### 4. Install QEMU via ESP-IDF Tools

ESP-IDF provides pre-built QEMU binaries. Install them using:

```bash
# Install QEMU for Xtensa architecture (ESP32, ESP32-S3)
python $IDF_PATH/tools/idf_tools.py install qemu-xtensa qemu-riscv32

# IMPORTANT: Re-source the environment to add QEMU to PATH
cd $IDF_PATH
. ./export.sh
```

### 5. Install pytest-embedded

Install the testing framework:

```bash
# From repository root
make esp-qemu-test-install

# Or manually:
pip install -r espidf/test/requirements.txt
```

## Running Tests

### Quick Start

```bash
# From repository root - runs all QEMU tests
make esp-qemu-test

# First time setup
. $HOME/esp/esp-idf/export.sh   # Source ESP-IDF environment
make esp-qemu-test-install       # Install pytest-embedded
make esp-qemu-test               # Run tests
```

### Available Test Targets

```bash
make esp-qemu-test-install   # Install pytest-embedded dependencies
make esp-qemu-test-build     # Build the test application (automatic with test targets)
make esp-qemu-test           # Run all QEMU tests (ESP32 + ESP32-S3)
make esp-qemu-test-esp32     # Run ESP32 tests only
make esp-qemu-test-esp32s3   # Run ESP32-S3 tests only
make esp-qemu-test-clean     # Clean test artifacts
```

**Note**: The test targets automatically copy sources, set up symlinks, and
build the application before running tests. You can run
`make esp-qemu-test-build` separately to only build without running tests.

### Manual pytest Execution

For more control, run pytest directly:

```bash
cd espidf/test

# Build the application first (required before first run)
idf.py build

# Run all QEMU tests
pytest --embedded-services idf,qemu -m qemu

# Run ESP32 tests only
pytest --embedded-services idf,qemu -m "esp32 and qemu" --target esp32

# Run with verbose output
pytest --embedded-services idf,qemu -m qemu -v

# Run specific test
pytest --embedded-services idf,qemu test_tamp_qemu.py::test_tamp_qemu
```

## Test Structure

### Test Files

- `pytest_tamp_qemu.py` - Main pytest test file with QEMU tests
- `pytest.ini` - pytest configuration
- `requirements.txt` - Python dependencies (pytest-embedded)
- `main/` - ESP-IDF test application (Unity tests)
  - `test_main.c` - Unity test runner
  - `test_esp32_compressor.c` - Compressor tests
  - `test_esp32_decompressor.c` - Decompressor tests

### How It Works

1. **pytest-embedded** handles all QEMU complexity automatically
2. Builds the ESP-IDF project (`idf.py build`)
3. Starts QEMU with the correct configuration
4. Captures serial output
5. Uses `dut.expect()` to verify test results
6. Provides clean pass/fail reporting

## What's Tested

1. **Compressor Tests**:

   - Initialization
   - Simple compression ("foo foo foo")
   - Repeated pattern compression

2. **Decompressor Tests**:

   - Byte-by-byte decompression
   - Bulk decompression

3. **ESP32-Specific Features**:
   - ESP32-S3 SIMD optimizations (when running on esp32s3 target)
   - ESP32 optimized compression algorithm

## Troubleshooting

### ESP-IDF environment not sourced

```bash
Error: IDF_PATH is not set
```

**Solution**: Source the ESP-IDF environment:

```bash
. $HOME/esp/esp-idf/export.sh
```

### QEMU not available

If you get "qemu-system-xtensa not found" errors:

**Solution 1**: Re-source the ESP-IDF environment after installing QEMU:

```bash
cd $IDF_PATH
. ./export.sh

# Verify QEMU is now available:
which qemu-system-xtensa
```

**Solution 2**: Install QEMU via ESP-IDF tools (if not installed):

```bash
python $IDF_PATH/tools/idf_tools.py install qemu-xtensa qemu-riscv32
cd $IDF_PATH
. ./export.sh
```

**Solution 3**: Install missing system dependencies (see Prerequisites section)

### pytest-embedded not installed

```bash
ModuleNotFoundError: No module named 'pytest_embedded'
```

**Solution**: Install dependencies:

```bash
make esp-qemu-test-install
# Or: pip install -r espidf/test/requirements.txt
```

### Build failures

If you encounter build errors, try cleaning and rebuilding:

```bash
make esp-qemu-test-clean
make esp-qemu-test
```

### Tests timeout

If tests timeout, you may need to increase the timeout in `pytest_tamp_qemu.py`:

```python
dut.expect("ALL TESTS PASSED", timeout=60)  # Increase from 30
```

## CI Integration

To integrate these tests into CI:

```yaml
- name: Install ESP-IDF
  # ... setup ESP-IDF ...

- name: Install QEMU
  run: |
    python $IDF_PATH/tools/idf_tools.py install qemu-xtensa qemu-riscv32

- name: Install test dependencies
  run: make esp-qemu-test-install

- name: Run QEMU tests
  run: |
    . $IDF_PATH/export.sh
    make esp-qemu-test
```

## Why pytest-embedded?

**pytest-embedded** is Espressif's official testing framework and provides:

- ✅ **Official Support** - Maintained by Espressif
- ✅ **Automatic QEMU Management** - Handles all QEMU complexity
- ✅ **Better Test Reporting** - Standard pytest output
- ✅ **CI/CD Ready** - Used in ESP-IDF's own CI
- ✅ **Less Maintenance** - No custom bash scripts
- ✅ **Multi-target Support** - Easy parameterization for different chips

For more information, see the
[pytest-embedded documentation](https://github.com/espressif/pytest-embedded).
