"""Micropython code to be ran on-device.
"""
import gc
import time

import tamp


def main():
    block_size = 4096
    decompressed_len = 0
    compressed_len = 0

    # Pre-allocate buffer to reduce memory fragmentation
    gc.collect()
    chunk = bytearray(block_size)

    start_ms = time.ticks_ms()
    with open("enwik8-100kb", "rb") as input_file, tamp.open("enwik8-100kb.tamp", "wb") as compressed_f:
        while True:
            n = input_file.readinto(chunk)
            if n == 0:
                break
            decompressed_len += n
            compressed_len += compressed_f.write(chunk if n == block_size else chunk[:n])
        compressed_len += compressed_f.flush(write_token=False)
    elapsed_ms = time.ticks_diff(time.ticks_ms(), start_ms)

    elapsed_s = elapsed_ms / 1000
    bytes_per_sec = decompressed_len / elapsed_s if elapsed_s > 0 else 0

    print(f"{decompressed_len=:,}")
    print(f"{compressed_len=:,}")
    print(f"elapsed={elapsed_s:.3f}s")
    print(f"compression={bytes_per_sec:,.0f} bytes/s")


if __name__ == "__main__":
    main()
