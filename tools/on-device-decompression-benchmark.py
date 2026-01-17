"""Micropython code to be ran on-device.
"""
import gc
import time

import tamp


def main():
    block_size = 1024
    decompressed_len = 0

    # Pre-allocate buffer to reduce memory fragmentation
    gc.collect()
    chunk = bytearray(block_size)

    start_ms = time.ticks_ms()
    with tamp.open("enwik8-100kb.tamp", "rb") as input_file, open("enwik8-100kb-decompressed", "wb") as output_file:
        while True:
            n = input_file.readinto(chunk)
            if n == 0:
                break
            decompressed_len += n
            output_file.write(chunk if n == block_size else chunk[:n])
    elapsed_ms = time.ticks_diff(time.ticks_ms(), start_ms)

    elapsed_s = elapsed_ms / 1000
    bytes_per_sec = decompressed_len / elapsed_s if elapsed_s > 0 else 0

    print(f"{decompressed_len=:,}")
    print(f"elapsed={elapsed_s:.3f}s")
    print(f"decompression={bytes_per_sec:,.0f} bytes/s")


if __name__ == "__main__":
    main()
