#!/usr/bin/env bash
# Reproduce the "Code size (B)" column in devices/BENCHMARKS.md.
#
# For each build config in that table, compiles the vendored objects
# (common.o + compressor.o + decompressor.o, plus compressor_esp32.o for
# TAMP_ESP32=1 rows) with exactly the row's documented CPU/opt/flags, and
# reports the sum of `text` (Berkeley `size`, includes .rodata) across them.
# Object-level only: no linking, no --gc-sections.
#
# Toolchains are located automatically and any that are missing are skipped
# (with a message) rather than failing the whole run:
#   - arm-none-eabi-gcc     must be on PATH
#   - xtensa-esp-elf-gcc    newest dir under ~/.espressif/tools/xtensa-esp-elf/
#   - riscv32-esp-elf-gcc   newest dir under ~/.espressif/tools/riscv32-esp-elf/
# The TAMP_ESP32=1 xtensa rows additionally need an esp-idf checkout (for
# components/xtensa/<target>/include/xtensa/config/core-isa.h, required to
# compile espidf/tamp/compressor_esp32.cpp for a real xtensa target); set
# IDF_PATH or have a checkout at ~/sdk/esp-idf or ~/esp/esp-idf.
#
# Usage: tools/benchmark-code-size.sh
set -euo pipefail
cd "$(dirname "$0")/.."

SCRATCH=build/code-size
rm -rf "$SCRATCH"
mkdir -p "$SCRATCH"
cleanup() { rm -rf "$SCRATCH"; }
trap cleanup EXIT

TAMP_SRC=tamp/_c_src/tamp
ESP32_CPP=espidf/tamp/compressor_esp32.cpp
ESP32_DEFINES=(-DTAMP_ESP32=1 -DTAMP_LAZY_MATCHING=1)
ESP32_INC=(-Itamp/_c_src/tamp -Iespidf/tamp -Ifuzz/esp32_host)

# ---------------------------------------------------------------- reporting
# Compiles common.o/compressor.o/decompressor.o (and optionally
# compressor_esp32.o) into $SCRATCH/<slug>, prints the size tool's own
# per-object breakdown, and sums the `text` column across all objects.
report() {
    local label="$1" size_tool="$2"
    shift 2
    local out
    out=$("$size_tool" "$@")
    echo "=== $label ==="
    echo "$out"
    local total
    total=$(echo "$out" | awk 'NR>1 {sum += $1} END {print sum + 0}')
    echo "total text: $total"
    echo
}

compile_c() {
    # compile_c <compiler> <outfile> <srcfile> <flags...>
    local cc="$1" out="$2" src="$3"
    shift 3
    "$cc" "$@" -Itamp/_c_src -c "$src" -o "$out"
}

# ------------------------------------------------------------ ARM (rp2040)
ARM_GCC=""
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    ARM_GCC=$(command -v arm-none-eabi-gcc)
fi
ARM_SIZE=""
if command -v arm-none-eabi-size >/dev/null 2>&1; then
    ARM_SIZE=$(command -v arm-none-eabi-size)
fi

if [ -z "$ARM_GCC" ] || [ -z "$ARM_SIZE" ]; then
    echo "SKIP: arm-none-eabi-gcc/-size not found on PATH; skipping rp2040 and stm32h7b0 configs."
else
    echo "Using arm-none-eabi-gcc: $ARM_GCC"

    run_arm() {
        local slug="$1" label="$2"
        shift 2
        local dir="$SCRATCH/$slug"
        mkdir -p "$dir"
        compile_c "$ARM_GCC" "$dir/common.o" "$TAMP_SRC/common.c" "$@"
        compile_c "$ARM_GCC" "$dir/compressor.o" "$TAMP_SRC/compressor.c" "$@"
        compile_c "$ARM_GCC" "$dir/decompressor.o" "$TAMP_SRC/decompressor.c" "$@"
        report "$label" "$ARM_SIZE" "$dir/common.o" "$dir/compressor.o" "$dir/decompressor.o"
    }

    run_arm rp2040_default "rp2040 default (RP2040, C, -O3)" \
        -O3 -mcpu=cortex-m0plus -mthumb

    run_arm rp2040_fastloop "rp2040 fastloop (RP2040, C, -O3, TAMP_FAST_DECODE_LOOP=1 TAMP_RESERVOIR_REFILL=1)" \
        -O3 -mcpu=cortex-m0plus -mthumb -DTAMP_FAST_DECODE_LOOP=1 -DTAMP_RESERVOIR_REFILL=1

    run_arm rp2040_mpy_native "rp2040 mpy-native (RP2040, MicroPython native module)" \
        -O2 -mcpu=cortex-m0plus -mthumb -DTAMP_STREAM=0 -DTAMP_USE_MEMSET=0

    run_arm stm32h7b0_portable "stm32h7b0 portable (Cortex-M7, C, -O3)" \
        -O3 -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16

    run_arm stm32h7b0_armv7em "stm32h7b0 armv7em (Cortex-M7, C, -O3, TAMP_ARMV7EM=1)" \
        -O3 -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16 -DTAMP_ARMV7EM=1
fi

# --------------------------------------------------------------- xtensa/esp
find_latest_gcc() {
    # $1 = glob pattern. Prints the lexicographically-last match, or nothing.
    if compgen -G "$1" >/dev/null 2>&1; then
        compgen -G "$1" | sort | tail -1
    fi
}

XTENSA_GCC=$(find_latest_gcc "$HOME/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp-elf-gcc")
RISCV_GCC=$(find_latest_gcc "$HOME/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin/riscv32-esp-elf-gcc")

# esp-idf checkout, needed only to compile compressor_esp32.cpp for a real
# xtensa target (it pulls in xtensa/config/core-isa.h, which ships as part of
# esp-idf's xtensa component, not the bare toolchain).
IDF_DIR=""
for candidate in "${IDF_PATH:-}" "$HOME/sdk/esp-idf" "$HOME/esp/esp-idf"; do
    if [ -n "$candidate" ] && [ -d "$candidate/components/xtensa" ]; then
        IDF_DIR="$candidate"
        break
    fi
done

if [ -z "$XTENSA_GCC" ]; then
    echo "SKIP: no xtensa-esp-elf-gcc found under ~/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/; skipping esp32 and esp32s3 configs."
else
    XTENSA_BIN=$(dirname "$XTENSA_GCC")
    XTENSA_SIZE="$XTENSA_BIN/xtensa-esp-elf-size"
    echo "Using xtensa-esp-elf-gcc: $XTENSA_GCC"
    if [ -z "$IDF_DIR" ]; then
        echo "NOTE: no esp-idf checkout found (checked \$IDF_PATH, ~/sdk/esp-idf, ~/esp/esp-idf);" \
            "the TAMP_ESP32=1 xtensa rows (which compile compressor_esp32.cpp for the real xtensa" \
            "target) will be skipped. Set IDF_PATH to a checkout to include them."
    else
        echo "Using esp-idf checkout for xtensa/config/core-isa.h: $IDF_DIR"
    fi

    run_xtensa_default() {
        # run_xtensa_default <slug> <label> <per-target-gcc>
        local slug="$1" label="$2" cc="$3"
        local dir="$SCRATCH/$slug"
        mkdir -p "$dir"
        # No -mcpu=esp32/-mcpu=esp32s3: xtensa-esp-elf-gcc rejects that spelling
        # ('unrecognized command-line option'); the per-target driver binary
        # (xtensa-esp32-elf-gcc / xtensa-esp32s3-elf-gcc) bakes in the target
        # config instead, so no cpu flag is passed or needed.
        compile_c "$cc" "$dir/common.o" "$TAMP_SRC/common.c" -O2
        compile_c "$cc" "$dir/compressor.o" "$TAMP_SRC/compressor.c" -O2
        compile_c "$cc" "$dir/decompressor.o" "$TAMP_SRC/decompressor.c" -O2
        report "$label" "$XTENSA_SIZE" "$dir/common.o" "$dir/compressor.o" "$dir/decompressor.o"
    }

    run_tamp_esp32_xtensa() {
        # run_tamp_esp32_xtensa <slug> <label> <per-target-gcc> <per-target-g++> <idf-target-dir>
        local slug="$1" label="$2" cc="$3" cxx="$4" idf_target="$5"
        if [ -z "$IDF_DIR" ]; then
            echo "SKIP: $label (needs esp-idf checkout; see NOTE above)"
            echo
            return
        fi
        local dir="$SCRATCH/$slug"
        mkdir -p "$dir"
        compile_c "$cc" "$dir/common.o" "$TAMP_SRC/common.c" -O2 "${ESP32_DEFINES[@]}"
        compile_c "$cc" "$dir/compressor.o" "$TAMP_SRC/compressor.c" -O2 "${ESP32_DEFINES[@]}" "${ESP32_INC[@]}"
        compile_c "$cc" "$dir/decompressor.o" "$TAMP_SRC/decompressor.c" -O2 "${ESP32_DEFINES[@]}" "${ESP32_INC[@]}"
        "$cxx" -std=c++20 -O2 "${ESP32_DEFINES[@]}" "${ESP32_INC[@]}" -Itamp/_c_src \
            -I "$IDF_DIR/components/xtensa/$idf_target/include" \
            -I "$IDF_DIR/components/xtensa/include" \
            -c "$ESP32_CPP" -o "$dir/compressor_esp32.o"
        report "$label" "$XTENSA_SIZE" \
            "$dir/common.o" "$dir/compressor.o" "$dir/decompressor.o" "$dir/compressor_esp32.o"
    }

    run_xtensa_default esp32_default "esp32 default (Xtensa LX6, -O2)" "$XTENSA_BIN/xtensa-esp32-elf-gcc"
    run_tamp_esp32_xtensa esp32_tamp_esp32 "esp32 TAMP_ESP32=1 (Xtensa LX6, -O2)" \
        "$XTENSA_BIN/xtensa-esp32-elf-gcc" "$XTENSA_BIN/xtensa-esp32-elf-g++" esp32

    run_xtensa_default esp32s3_default "esp32s3 default (Xtensa LX7, -O2)" "$XTENSA_BIN/xtensa-esp32s3-elf-gcc"
    run_tamp_esp32_xtensa esp32s3_tamp_esp32 "esp32s3 TAMP_ESP32=1 (Xtensa LX7, -O2)" \
        "$XTENSA_BIN/xtensa-esp32s3-elf-gcc" "$XTENSA_BIN/xtensa-esp32s3-elf-g++" esp32s3
fi

if [ -z "$RISCV_GCC" ]; then
    echo "SKIP: no riscv32-esp-elf-gcc found under ~/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin/; skipping esp32c3 configs."
else
    RISCV_BIN=$(dirname "$RISCV_GCC")
    RISCV_GXX="$RISCV_BIN/riscv32-esp-elf-g++"
    RISCV_SIZE="$RISCV_BIN/riscv32-esp-elf-size"
    echo "Using riscv32-esp-elf-gcc: $RISCV_GCC"

    RISCV_ARCH=(-march=rv32imc_zicsr -mabi=ilp32)
    if ! "$RISCV_GCC" "${RISCV_ARCH[@]}" -c "$TAMP_SRC/common.c" -Itamp/_c_src -o "$SCRATCH/riscv_arch_probe.o" 2>/dev/null; then
        echo "NOTE: -march=rv32imc_zicsr rejected by this riscv32-esp-elf-gcc; falling back to -march=rv32imc."
        RISCV_ARCH=(-march=rv32imc -mabi=ilp32)
    fi

    dir="$SCRATCH/esp32c3_default"
    mkdir -p "$dir"
    compile_c "$RISCV_GCC" "$dir/common.o" "$TAMP_SRC/common.c" -O2 "${RISCV_ARCH[@]}"
    compile_c "$RISCV_GCC" "$dir/compressor.o" "$TAMP_SRC/compressor.c" -O2 "${RISCV_ARCH[@]}"
    compile_c "$RISCV_GCC" "$dir/decompressor.o" "$TAMP_SRC/decompressor.c" -O2 "${RISCV_ARCH[@]}"
    report "esp32c3 default (RISC-V RV32IMC, -O2)" "$RISCV_SIZE" \
        "$dir/common.o" "$dir/compressor.o" "$dir/decompressor.o"

    dir="$SCRATCH/esp32c3_tamp_esp32"
    mkdir -p "$dir"
    compile_c "$RISCV_GCC" "$dir/common.o" "$TAMP_SRC/common.c" -O2 "${RISCV_ARCH[@]}" "${ESP32_DEFINES[@]}"
    compile_c "$RISCV_GCC" "$dir/compressor.o" "$TAMP_SRC/compressor.c" -O2 "${RISCV_ARCH[@]}" "${ESP32_DEFINES[@]}" "${ESP32_INC[@]}"
    compile_c "$RISCV_GCC" "$dir/decompressor.o" "$TAMP_SRC/decompressor.c" -O2 "${RISCV_ARCH[@]}" "${ESP32_DEFINES[@]}" "${ESP32_INC[@]}"
    "$RISCV_GXX" -std=c++20 -O2 "${RISCV_ARCH[@]}" "${ESP32_DEFINES[@]}" "${ESP32_INC[@]}" -Itamp/_c_src \
        -c "$ESP32_CPP" -o "$dir/compressor_esp32.o"
    report "esp32c3 TAMP_ESP32=1 (RISC-V RV32IMC, -O2)" "$RISCV_SIZE" \
        "$dir/common.o" "$dir/compressor.o" "$dir/decompressor.o" "$dir/compressor_esp32.o"
fi
