#!/usr/bin/env bash
# Build and run the malicious-input decompressor fuzzer across every
# meaningful compile-time configuration of the decompressor, so no
# flag-gated code path ships unfuzzed. Each config replays the shared
# corpus (regression) and then fuzzes for FUZZ_SECONDS (default 90).
#
# All configs are built serially first (builds are quick), then fuzzed with
# bounded parallelism: FUZZ_JOBS run at a time (default: CPU count via
# nproc/sysctl, override with the FUZZ_JOBS env var). Each config's fuzz
# output goes to build/fuzz_matrix/log_<name>.txt; any crash/leak/timeout
# reproducer libFuzzer writes lands in build/fuzz_matrix/artifacts_<name>/,
# so parallel configs never collide on disk.
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
    # Shipping ARMV7EM profile via its own knob (NOT hand-expanded into the
    # sub-flags: the profile's definition lives in common.h alone, so this
    # entry keeps fuzzing whatever the profile means as it evolves). The
    # profile's compressor-side match selection is inert here - only
    # decompressor.c+common.c are compiled.
    "v7em:-DTAMP_ARMV7EM=1"
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

default_jobs() {
    if command -v nproc >/dev/null 2>&1; then
        nproc
    elif command -v sysctl >/dev/null 2>&1; then
        sysctl -n hw.ncpu
    else
        echo 1
    fi
}
FUZZ_JOBS=${FUZZ_JOBS:-$(default_jobs)}
case "$FUZZ_JOBS" in
    ''|*[!0-9]*) FUZZ_JOBS=1 ;;
esac
[ "$FUZZ_JOBS" -ge 1 ] || FUZZ_JOBS=1

mkdir -p build/fuzz_matrix

# --------------------------------------------------------------- build phase
echo "=== building ${#CONFIGS[@]} configs"
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
done

# --------------------------------------------------------------- fuzz phase
# Each config writes only to its own corpus workdir (the first, writable,
# libFuzzer-owned positional arg) and replays the shared $CORPUS read-only
# (second positional arg) — libFuzzer never writes into it — so configs never
# race each other on disk and can safely fuzz with bounded parallelism.
run_one() {
    local name="$1"
    local bin="build/fuzz_matrix/fuzz_decompressor_$name"
    local workdir="build/fuzz_matrix/corpus_$name"
    local artifacts="build/fuzz_matrix/artifacts_$name"
    local log="build/fuzz_matrix/log_$name.txt"
    mkdir -p "$workdir" "$artifacts"
    echo "=== [$name] corpus replay + ${FUZZ_SECONDS}s fuzz" >"$log"
    local status=0
    "$bin" -max_total_time="$FUZZ_SECONDS" -print_final_stats=1 \
        -artifact_prefix="$artifacts/" \
        "$workdir" "$CORPUS" >>"$log" 2>&1 || status=$?
    if [ "$status" -eq 0 ]; then
        echo pass >"build/fuzz_matrix/status_$name.txt"
    else
        echo fail >"build/fuzz_matrix/status_$name.txt"
    fi
    return "$status"
}

echo "=== fuzzing ${#CONFIGS[@]} configs, FUZZ_JOBS=$FUZZ_JOBS, ${FUZZ_SECONDS}s each"
idx=0
total=${#CONFIGS[@]}
while [ "$idx" -lt "$total" ]; do
    batch_pids=()
    batch_names=()
    slots=0
    while [ "$slots" -lt "$FUZZ_JOBS" ] && [ "$idx" -lt "$total" ]; do
        entry=${CONFIGS[$idx]}
        name=${entry%%:*}
        echo "=== [$name] launched"
        run_one "$name" &
        batch_pids+=("$!")
        batch_names+=("$name")
        idx=$((idx + 1))
        slots=$((slots + 1))
    done
    for k in "${!batch_pids[@]}"; do
        pid=${batch_pids[$k]}
        cname=${batch_names[$k]}
        if wait "$pid"; then
            echo "=== [$cname] done"
        else
            echo "!!! [$cname] FAILED (see build/fuzz_matrix/log_$cname.txt)"
        fi
    done
done

# -------------------------------------------------------------------- summary
echo
echo "=== summary"
fail=0
for entry in "${CONFIGS[@]}"; do
    name=${entry%%:*}
    status_file="build/fuzz_matrix/status_$name.txt"
    status="unknown"
    [ -f "$status_file" ] && status=$(cat "$status_file")
    if [ "$status" = "pass" ]; then
        echo "PASS  $name"
    else
        fail=1
        echo "FAIL  $name  (log: build/fuzz_matrix/log_$name.txt, artifacts: build/fuzz_matrix/artifacts_$name/)"
    fi
done
exit $fail
