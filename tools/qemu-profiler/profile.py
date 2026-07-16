#!/usr/bin/env python3
"""QEMU-based embedded decompression profiler.

Builds the bare-metal benchmark firmware for several Cortex-M cores, runs each
under qemu-system-arm (MPS2 machines) with the insncount TCG plugin, and
reports deterministic per-function executed-instruction profiles.

The metric that matters for A/B comparisons is "core insns/out-byte": executed
instructions attributed to the decompressor hot code, divided by decompressed
bytes. It is exact and noise-free (single vCPU, no interrupts), so any delta
between two builds is real.

Usage:
    python3 profile.py                       # all cores, classic blob
    python3 profile.py --cores m7 --top 20
    python3 profile.py --cflags "-DTAMP_EXPERIMENT_FOO=1" --json exp.json
    python3 profile.py --compare base.json exp.json
"""

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
C_SRC = REPO / "tamp" / "_c_src"
RP2040_DATA = REPO / "devices" / "rp2040"
BUILD = HERE / "build"

QEMU = "qemu-system-arm"
CC_HOST = "cc"
CROSS = "arm-none-eabi-"

# Code region watched by the plugin (must cover .text in link.ld's CODE region)
CODE_BASE = 0x0
CODE_SIZE = 0x100000

CORES = {
    "m0plus": {
        "machine": "mps2-an385",  # M3 core; executes the armv6m instruction stream fine
        "mcpu": "cortex-m0plus",
        "defines": [],
        "note": "portable build (RP2040-like)",
    },
    "m3": {
        "machine": "mps2-an385",
        "mcpu": "cortex-m3",
        "defines": [],
        "note": "portable build",
    },
    "m4": {
        "machine": "mps2-an386",
        "mcpu": "cortex-m4",
        "defines": ["TAMP_ARMV7EM=1"],
        "note": "armv7em profile",
    },
    "m7": {
        "machine": "mps2-an500",
        "mcpu": "cortex-m7",
        "defines": ["TAMP_ARMV7EM=1"],
        "note": "armv7em profile (STM32H7-like)",
    },
}

VARIANTS = {
    "classic": {"data_h": "bench_data_classic.h"},
    "extended": {"data_h": "bench_data_extended.h"},  # generated, see gen_extended_blob()
}

# Symbols considered "the decompressor" for the headline metric.
CORE_SYMBOL_PREFIXES = (
    "tamp_decompressor_decompress_cb",
    "tamp_window_copy",
    "decode_rle",
    "decode_extended_match",
    "decode_huffman",
    "refill_bit_buffer",
)


def run(cmd, **kw):
    kw.setdefault("check", True)
    return subprocess.run(cmd, **kw)


def build_plugin():
    out = BUILD / "insncount.dylib"
    src = HERE / "plugin" / "insncount.c"
    if out.exists() and out.stat().st_mtime > src.stat().st_mtime:
        return out
    qemu_inc = subprocess.run(
        ["sh", "-c", "dirname $(find $(brew --prefix qemu)/include -name qemu-plugin.h | head -1)"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()
    glib_cflags = subprocess.run(
        ["pkg-config", "--cflags", "glib-2.0"], capture_output=True, text=True, check=True
    ).stdout.split()
    BUILD.mkdir(parents=True, exist_ok=True)
    run(
        [
            CC_HOST,
            "-O2",
            "-dynamiclib",
            "-undefined",
            "dynamic_lookup",
            f"-I{qemu_inc}",
            *glib_cflags,
            "-o",
            str(out),
            str(src),
        ]
    )
    return out


def gen_extended_blob():
    """Generate an extended-format compressed blob header via the tamp CLI."""
    out_h = BUILD / "bench_data_extended.h"
    enwik8_100k = BUILD / "enwik8_100k"
    blob = BUILD / "enwik8_100k_ext.tamp"
    if out_h.exists():
        return out_h.parent
    dataset = REPO / "datasets" / "enwik8"
    if not dataset.exists():
        sys.exit(f"extended variant needs {dataset} (make download-enwik8)")
    BUILD.mkdir(parents=True, exist_ok=True)
    enwik8_100k.write_bytes(dataset.read_bytes()[:100000])
    run(["uv", "run", "tamp", "compress", str(enwik8_100k), "-o", str(blob), "-w", "10"], cwd=REPO)
    data = blob.read_bytes()
    if not (data[0] & 0x2):
        sys.exit("generated blob is not extended format; check tamp CLI flags")
    lines = [
        "/* Auto-generated: extended-format enwik8-100KB blob, window=10. */",
        '#include "enwik8.h"',
        "static const unsigned char ENWIK8_EXT_COMPRESSED[] = {",
    ]
    for i in range(0, len(data), 16):
        lines.append(" ".join(f"0x{b:02x}," for b in data[i : i + 16]))
    lines += [
        "};",
        "#define BENCH_INPUT ENWIK8_EXT_COMPRESSED",
        "#define BENCH_INPUT_SIZE sizeof(ENWIK8_EXT_COMPRESSED)",
        "#define BENCH_EXPECTED ENWIK8",
        "#define BENCH_EXPECTED_SIZE sizeof(ENWIK8)",
        "#define BENCH_OUTPUT_SIZE 100000",
        "",
    ]
    out_h.write_text("\n".join(lines))
    return out_h.parent


def build_firmware(core, variant, extra_cflags, main_src="main.c", elf_name="bench.elf"):
    cfg = CORES[core]
    outdir = BUILD / f"{core}-{variant}"
    outdir.mkdir(parents=True, exist_ok=True)
    elf = outdir / elf_name
    fw = HERE / "firmware"
    srcs = [
        fw / main_src,
        fw / "startup.c",
        fw / "semihost.c",
        C_SRC / "tamp" / "common.c",
        C_SRC / "tamp" / "decompressor.c",
    ]
    cmd = [
        CROSS + "gcc",
        f"-mcpu={cfg['mcpu']}",
        "-mthumb",
        "-mfloat-abi=soft",
        "-O3",
        "-g",
        "-Wall",
        f"-I{C_SRC}",
        f"-I{RP2040_DATA}",
        f"-I{fw}",
        f"-I{BUILD}",
        f'-DBENCH_DATA_H="{VARIANTS[variant]["data_h"]}"',
        *[f"-D{d}" for d in cfg["defines"]],
        *extra_cflags,
        "-T",
        str(fw / "link.ld"),
        "-nostartfiles",
        "--specs=nosys.specs",
        "-Wl,--gc-sections",
        "-ffunction-sections",
        "-fdata-sections",
        "-o",
        str(elf),
        *map(str, srcs),
    ]
    run(cmd)
    return elf


def run_qemu(core, elf, plugin, out_file):
    cfg = CORES[core]
    cmd = [
        QEMU,
        "-M",
        cfg["machine"],
        "-nographic",
        "-monitor",
        "none",
        "-semihosting-config",
        "enable=on,target=native",
        "-plugin",
        f"{plugin},out={out_file},base={CODE_BASE:#x},size={CODE_SIZE:#x}",
        "-kernel",
        str(elf),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    return proc


def read_symbols(elf):
    """Return sorted list of (addr, size, name) for function symbols."""
    out = subprocess.run([CROSS + "nm", "-S", str(elf)], capture_output=True, text=True, check=True).stdout
    syms = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[2].lower() in ("t", "w"):
            addr = int(parts[0], 16) & ~1  # clear Thumb bit
            size = int(parts[1], 16)
            syms.append((addr, size, parts[3]))
    syms.sort()
    return syms


def attribute(counts_file, syms):
    """Aggregate per-PC counts into per-symbol counts."""
    per_symbol = {}
    total = other = 0
    addrs = [s[0] for s in syms]
    import bisect

    for line in Path(counts_file).read_text().splitlines():
        if line.startswith("# total"):
            total = int(line.split()[2])
            continue
        if line.startswith("# other"):
            other = int(line.split()[2])
            continue
        vaddr_s, count_s = line.split()
        vaddr, count = int(vaddr_s, 16), int(count_s)
        i = bisect.bisect_right(addrs, vaddr) - 1
        name = "<unknown>"
        if i >= 0:
            a, sz, nm = syms[i]
            name = nm if (sz == 0 or vaddr < a + sz) else f"<gap after {nm}>"
        per_symbol[name] = per_symbol.get(name, 0) + count
    return total, other, per_symbol


def core_insns(per_symbol):
    return sum(c for s, c in per_symbol.items() if s.startswith(CORE_SYMBOL_PREFIXES))


def profile_one(core, variant, plugin, extra_cflags, top, quiet=False):
    elf = build_firmware(core, variant, extra_cflags)
    counts_file = BUILD / f"{core}-{variant}" / "insncount.out"
    proc = run_qemu(core, elf, plugin, counts_file)
    stdout = proc.stdout + proc.stderr  # semihosting console may land on either
    passed = "QEMU-BENCH: PASS" in stdout
    if not passed:
        print(f"!! {core}/{variant}: FAILED (exit={proc.returncode})")
        print(stdout[-2000:])
        print(proc.stderr[-2000:])
        return None
    out_bytes = int(stdout.split("out_bytes=")[1].split()[0])
    syms = read_symbols(elf)
    total, other, per_symbol = attribute(counts_file, syms)
    core_total = core_insns(per_symbol)
    result = {
        "core": core,
        "variant": variant,
        "machine": CORES[core]["machine"],
        "out_bytes": out_bytes,
        "total_insns": total,
        "other_insns": other,
        "core_insns": core_total,
        "core_insns_per_byte": core_total / out_bytes,
        "per_symbol": dict(sorted(per_symbol.items(), key=lambda kv: -kv[1])),
    }
    if not quiet:
        print(f"\n== {core} ({CORES[core]['machine']}, {CORES[core]['note']}) / {variant} ==")
        print(
            f"  guest insns total: {total:,}   decompressor core: {core_total:,}"
            f"   core insns/byte: {core_total / out_bytes:.3f}"
        )
        width = max((len(s) for s in list(result["per_symbol"])[:top]), default=10)
        for s, c in list(result["per_symbol"].items())[:top]:
            mark = "*" if s.startswith(CORE_SYMBOL_PREFIXES) else " "
            print(f"  {mark} {s:<{width}} {c:>12,}  {c / out_bytes:8.3f}/B  {100.0 * c / total:5.1f}%")
    return result


def pack_corpus(corpus_dir):
    """Pack a fuzz corpus directory into the length-prefixed replay blob."""
    blob = REPO / "build" / "qemu-corpus.bin"
    files = sorted(p for p in Path(corpus_dir).iterdir() if p.is_file())
    with blob.open("wb") as f:
        f.write(len(files).to_bytes(4, "little"))
        for p in files:
            data = p.read_bytes()
            f.write(len(data).to_bytes(4, "little"))
            f.write(data)
    return len(files)


def replay(corpus_dir, cores, extra_cflags):
    """Replay an adversarial corpus through the real ARM binaries under QEMU.

    Catches ISA/ABI-specific defects host ASAN fuzzing cannot (target codegen,
    unsigned plain char, 32-bit size_t); the firmware canary-fences its
    buffers and faults report via the vector handlers.
    """
    n = pack_corpus(corpus_dir)
    print(f"packed {n} corpus entries from {corpus_dir}")
    ok = True
    for core in cores:
        elf = build_firmware(core, "classic", extra_cflags, main_src="replay_main.c", elf_name="replay.elf")
        cfg = CORES[core]
        cmd = [
            QEMU,
            "-M",
            cfg["machine"],
            "-nographic",
            "-monitor",
            "none",
            "-semihosting-config",
            "enable=on,target=native",
            "-kernel",
            str(elf),
        ]
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=1800, cwd=REPO)
        out = proc.stdout + proc.stderr
        line = next((ln for ln in out.splitlines() if "QEMU-REPLAY:" in ln), "no QEMU-REPLAY line")
        print(f"{core} ({cfg['machine']}, {cfg['note']}): {line.split('QEMU-REPLAY: ')[-1]}")
        if "QEMU-REPLAY: PASS" not in out:
            print(out[-1500:])
            ok = False
    return ok


def annotate(core, variant, symbol, threshold=0.0):
    """Print disassembly of `symbol` with per-instruction executed counts."""
    outdir = BUILD / f"{core}-{variant}"
    elf = outdir / "bench.elf"
    counts_file = outdir / "insncount.out"
    counts = {}
    for line in Path(counts_file).read_text().splitlines():
        if line.startswith("#"):
            continue
        vaddr_s, count_s = line.split()
        counts[int(vaddr_s, 16)] = int(count_s)
    dis = subprocess.run(
        [CROSS + "objdump", "-d", f"--disassemble={symbol}", str(elf)], capture_output=True, text=True, check=True
    ).stdout
    max_count = max(counts.values(), default=1)
    for line in dis.splitlines():
        stripped = line.strip()
        if ":" in stripped:
            addr_part = stripped.split(":")[0].strip()
            try:
                addr = int(addr_part, 16)
            except ValueError:
                print(line)
                continue
            c = counts.get(addr)
            if c is not None:
                if c < threshold * max_count:
                    continue
                print(f"{c:>12,}  {line}")
            else:
                print(f"{'':>12}  {line}")
        else:
            print(line)


def compare(base_path, exp_path):
    base = json.loads(Path(base_path).read_text())
    exp = json.loads(Path(exp_path).read_text())
    bidx = {(r["core"], r["variant"]): r for r in base["results"]}
    for r in exp["results"]:
        key = (r["core"], r["variant"])
        b = bidx.get(key)
        if not b:
            continue
        db = r["core_insns_per_byte"] - b["core_insns_per_byte"]
        pct = 100.0 * db / b["core_insns_per_byte"]
        print(
            f"{r['core']}/{r['variant']}: {b['core_insns_per_byte']:.3f} -> "
            f"{r['core_insns_per_byte']:.3f} insns/B  ({pct:+.2f}%)"
        )
        allsyms = sorted(
            set(b["per_symbol"]) | set(r["per_symbol"]),
            key=lambda s: -(abs(r["per_symbol"].get(s, 0) - b["per_symbol"].get(s, 0))),
        )
        for s in allsyms[:8]:
            vb, ve = b["per_symbol"].get(s, 0), r["per_symbol"].get(s, 0)
            if vb == ve:
                continue
            print(f"    {s}: {vb:,} -> {ve:,} ({ve - vb:+,})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cores", default="m0plus,m4,m7")
    ap.add_argument("--variants", default="classic")
    ap.add_argument("--cflags", default="", help="extra CFLAGS for firmware build")
    ap.add_argument("--top", type=int, default=12)
    ap.add_argument("--json", type=Path, help="write results JSON here")
    ap.add_argument("--compare", nargs=2, metavar=("BASE", "EXP"), help="diff two JSON result files")
    ap.add_argument(
        "--annotate",
        metavar="SYMBOL",
        help="print count-annotated disassembly of SYMBOL from the last run (uses --cores/--variants first entry)",
    )
    ap.add_argument(
        "--replay",
        metavar="CORPUS_DIR",
        help="replay a fuzz corpus through the real ARM binaries on the emulated cores",
    )
    args = ap.parse_args()

    if args.compare:
        compare(*args.compare)
        return
    if args.annotate:
        annotate(args.cores.split(",")[0], args.variants.split(",")[0], args.annotate)
        return
    if args.replay:
        if not replay(args.replay, args.cores.split(","), args.cflags.split()):
            sys.exit(1)
        return

    if not shutil.which(QEMU):
        sys.exit("qemu-system-arm not found")
    plugin = build_plugin()
    extra_cflags = args.cflags.split()
    variants = args.variants.split(",")
    if "extended" in variants:
        gen_extended_blob()

    results = []
    for core in args.cores.split(","):
        for variant in variants:
            r = profile_one(core, variant, plugin, extra_cflags, args.top)
            if r:
                results.append(r)
    if args.json:
        args.json.write_text(json.dumps({"cflags": args.cflags, "results": results}, indent=1))
        print(f"\nwrote {args.json}")
    if len(results) < len(args.cores.split(",")) * len(variants):
        sys.exit(1)


if __name__ == "__main__":
    main()
