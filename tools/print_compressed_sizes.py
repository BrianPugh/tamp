"""
Print compressed sizes for test files used in optimize-extended-huffman.py.

This script compresses the same files that optimize-extended-huffman.py uses
and prints the compressed size for each file with thousands separators.
"""

from pathlib import Path

import tamp.compressor


def main():
    # Define test files (same as optimize-extended-huffman.py)
    build_dir = Path(__file__).parent.parent / "build"
    test_files = [build_dir / "enwik8", build_dir / "RPI_PICO-20250415-v1.25.0.uf2", *(build_dir / "silesia").iterdir()]
    test_files.sort()

    for file_path in test_files:
        # Read and compress the file
        data = file_path.read_bytes()
        if len(data) == 0:
            print(f"{file_path.name}: Empty file")
            continue

        compressed_data = tamp.compressor.compress(data)
        compressed_size = len(compressed_data)

        # Print with thousands separators
        print(f"{file_path.name}: {compressed_size:,} bytes")


if __name__ == "__main__":
    main()
