#!/usr/bin/env bash
# Build and run the malicious-input decompressor fuzzer across every
# meaningful compile-time configuration of the decompressor, so no
# flag-gated code path ships unfuzzed. Each config replays the shared
# corpus (regression) and then fuzzes for FUZZ_SECONDS (default 90).
#
# Usage: fuzz/fuzz-matrix.sh [FUZZ_SECONDS]
# Requires an LLVM clang with the libFuzzer runtime (brew install llvm).
set -euo pipefail
cd "$(dirname "$0")/.."

FUZZ_SECONDS=${1:-90}
FUZZ_CC=${FUZZ_CC:-}
if [ -z "$FUZZ_CC" ]; then
    for c in /opt/homebrew/opt/llvm/bin/clang /usr/local/opt/llvm/bin/clang clang; do
        [ -x "$c" ] || command -v "$c" >/dev/null 2>&1 || continue
        FUZZ_CC=$c
        break
    done
fi
BASE_FLAGS="-g -O1 -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer -Itamp/_c_src"
ESP32_FLAGS="-DTAMP_ESP32=1 -fno-strict-aliasing -fno-sanitize=alignment -Itamp/_c_src/tamp -Iespidf/tamp -Ifuzz/esp32_host"
SRC="fuzz/fuzz_decompressor.c tamp/_c_src/tamp/decompressor.c tamp/_c_src/tamp/common.c"
CORPUS=fuzz/corpus_decompressor

# name:flags — one entry per distinct decompressor code-path configuration.
CONFIGS=(
    "portable:"
    "v7em:-DTAMP_FAST_DECODE_LOOP=1 -DTAMP_WINDOW_FROM_OUTPUT=1 -DTAMP_FAST_WINDOW_COPY=1 -DTAMP_FAST_BIT_REFILL=1 -DTAMP_FAST_OUTPUT_COPY=1"
    "v7em_history:-DTAMP_FAST_DECODE_LOOP=1 -DTAMP_WINDOW_FROM_OUTPUT=1 -DTAMP_FAST_WINDOW_COPY=1 -DTAMP_FAST_BIT_REFILL=1 -DTAMP_FAST_OUTPUT_COPY=1 -DTAMP_HISTORY_WINDOW=1"
    "history_classic:-DTAMP_EXTENDED=0 -DTAMP_FAST_DECODE_LOOP=1 -DTAMP_HISTORY_WINDOW=1"
    "fastloop_only:-DTAMP_FAST_DECODE_LOOP=1"
    "window_from_output_portable:-DTAMP_WINDOW_FROM_OUTPUT=1"
    "no_extended:-DTAMP_EXTENDED=0"
    "no_extended_fastloop:-DTAMP_EXTENDED=0 -DTAMP_FAST_DECODE_LOOP=1"
    "no_memset:-DTAMP_USE_MEMSET=0"
    "esp32:ESP32"
    "esp32_fastloop:ESP32 -DTAMP_FAST_DECODE_LOOP=1"
    "fixed_w10:-DTAMP_FIXED_WINDOW_BITS=10 -DTAMP_FIXED_LITERAL_BITS=8"
    "fixed_w10_fastloop:-DTAMP_FIXED_WINDOW_BITS=10 -DTAMP_FIXED_LITERAL_BITS=8 -DTAMP_FAST_DECODE_LOOP=1 -DTAMP_WINDOW_FROM_OUTPUT=1 -DTAMP_FAST_WINDOW_COPY=1 -DTAMP_FAST_BIT_REFILL=1 -DTAMP_FAST_OUTPUT_COPY=1"
)

mkdir -p build/fuzz_matrix
fail=0
for entry in "${CONFIGS[@]}"; do
    name=${entry%%:*}
    flags=${entry#*:}
    extra=""
    if [[ $flags == ESP32* ]]; then
        extra="$ESP32_FLAGS ${flags#ESP32}"
        flags=""
    fi
    bin=build/fuzz_matrix/fuzz_decompressor_$name
    echo "=== [$name] build"
    # shellcheck disable=SC2086
    "$FUZZ_CC" $BASE_FLAGS $flags $extra -o "$bin" $SRC
    workdir=build/fuzz_matrix/corpus_$name
    mkdir -p "$workdir"
    echo "=== [$name] corpus replay + ${FUZZ_SECONDS}s fuzz"
    if ! "$bin" -max_total_time="$FUZZ_SECONDS" -print_final_stats=1 "$workdir" "$CORPUS" 2>&1 |
        tail -4 | sed "s/^/[$name] /"; then
        echo "!!! [$name] FAILED"
        fail=1
    fi
done
exit $fail
