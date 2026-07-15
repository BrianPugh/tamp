#!/usr/bin/env python3
"""Pack benchmark data into a single blob for debugger-loaded device harnesses.

Layout (little-endian u32s): magic "TAMP", input size, reference size, vectors
size, then the three payloads concatenated. Devices whose flash cannot embed
the data (e.g. STM32H7B0, 128K) read this from RAM after the runner loads it
over SWD; see devices/stm32h7b0/main.c for the consuming struct.
"""

import argparse
import pathlib
import struct

MAGIC = 0x504D4154  # "TAMP"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=pathlib.Path, help="Original data (first 100 KB of enwik8).")
    parser.add_argument("reference", type=pathlib.Path, help="Expected compressed output (v1 format).")
    parser.add_argument("vectors", type=pathlib.Path, help="Packed regression vectors (pack-device-vectors.py).")
    parser.add_argument("-o", "--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    payloads = [args.input.read_bytes(), args.reference.read_bytes(), args.vectors.read_bytes()]
    header = struct.pack("<4I", MAGIC, *(len(p) for p in payloads))
    args.output.write_bytes(header + b"".join(payloads))
    print(f"{args.output}: {len(header) + sum(len(p) for p in payloads)} bytes")


if __name__ == "__main__":
    main()
