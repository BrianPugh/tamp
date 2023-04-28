import argparse
import sys
import zlib
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    args = parser.parse_args()
    with args.input.open("rb") as f:
        decompressed_data = f.read()
        compressobj = zlib.compressobj(level=9, wbits=10, memLevel=1, strategy=zlib.Z_DEFAULT_STRATEGY)
        compressed_data = compressobj.compress(decompressed_data)
        compressed_data += compressobj.flush()
    sys.stdout.buffer.write(compressed_data)


if __name__ == "__main__":
    main()
