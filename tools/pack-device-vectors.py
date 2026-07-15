#!/usr/bin/env python3
"""Pack regression vectors into a flat binary for on-device replay.

Format (little-endian):
    u32   count
    per vector:
        u32   length
        bytes raw vector data

Empty input produces a valid count=0 file.
"""

import argparse
import struct
import sys
from pathlib import Path


def collect_files(inputs):
    files = []
    for item in inputs:
        path = Path(item)
        if path.is_dir():
            files.extend(sorted(p for p in path.iterdir() if p.is_file() and not p.name.startswith(".")))
        elif path.is_file():
            files.append(path)
        else:
            sys.exit(f"error: not a file or directory: {item}")
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="+", help="Vector files and/or directories of vector files.")
    parser.add_argument("-o", "--output", required=True, help="Output packed binary path.")
    args = parser.parse_args()

    files = collect_files(args.inputs)

    with open(args.output, "wb") as f:
        f.write(struct.pack("<I", len(files)))
        for path in files:
            data = path.read_bytes()
            f.write(struct.pack("<I", len(data)))
            f.write(data)

    print(f"Packed {len(files)} vector(s) -> {args.output}")


if __name__ == "__main__":
    main()
