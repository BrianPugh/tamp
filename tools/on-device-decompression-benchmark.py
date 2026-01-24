"""Micropython code to be ran on-device."""

import gc
import io
import time

import tamp


def main():
    block_size = 1024

    # Load compressed data into RAM before timing
    gc.collect()
    print("Loading compressed data into RAM...")
    with open("enwik8-100kb.tamp", "rb") as f:
        compressed_data = f.read()
    compressed_len = len(compressed_data)

    # Pre-allocate chunk buffer
    gc.collect()
    chunk = bytearray(block_size)

    # First pass: benchmark decompression (discard output)
    print("Decompressing (benchmark pass)...")
    decompressed_len = 0
    start_ms = time.ticks_ms()
    with tamp.open(io.BytesIO(compressed_data), "rb") as decompressor:
        while True:
            n = decompressor.readinto(chunk)
            if n == 0:
                break
            decompressed_len += n
    elapsed_ms = time.ticks_diff(time.ticks_ms(), start_ms)

    # Second pass: decompress and write to disk for verification (not timed)
    print("Writing to disk for verification...")
    with tamp.open(io.BytesIO(compressed_data), "rb") as decompressor, open("enwik8-100kb-decompressed", "wb") as f:
        while True:
            n = decompressor.readinto(chunk)
            if n == 0:
                break
            f.write(chunk[:n])

    elapsed_s = elapsed_ms / 1000
    bytes_per_sec = decompressed_len / elapsed_s if elapsed_s > 0 else 0

    print(f"{compressed_len=:,}")
    print(f"{decompressed_len=:,}")
    print(f"elapsed={elapsed_s:.3f}s")
    print(f"decompression={bytes_per_sec:,.0f} bytes/s")


if __name__ == "__main__":
    main()
