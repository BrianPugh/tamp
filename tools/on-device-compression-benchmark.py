"""Micropython code to be ran on-device."""

import gc
import os
import time

import tamp


def main():
    # Small enough to allocate on a fragmented ESP32 heap,
    # large enough to amortize readinto + write call overhead.
    block_size = 16384
    input_path = "enwik8-100kb"
    output_path = "enwik8-100kb.tamp"

    decompressed_len = os.stat(input_path)[6]

    gc.collect()
    read_buf = bytearray(block_size)
    mv = memoryview(read_buf)

    # Tare: flash-read cost alone, so we can subtract it from the full pass
    # and recover a pure-compression throughput number.
    print("Measuring flash-read tare...")
    start_us = time.ticks_us()
    with open(input_path, "rb") as f:
        while f.readinto(read_buf):
            pass
    read_elapsed_us = time.ticks_diff(time.ticks_us(), start_us)

    gc.collect()

    # Compression: read chunk -> feed compressor -> compressor writes to flash.
    out = open(output_path, "wb")
    print("Compressing...")
    try:
        start_us = time.ticks_us()
        with open(input_path, "rb") as f_in, tamp.open(out, "wb") as compressor:
            while True:
                n = f_in.readinto(read_buf)
                if not n:
                    break
                compressor.write(mv[:n])
        total_elapsed_us = time.ticks_diff(time.ticks_us(), start_us)
    finally:
        out.close()

    compressed_len = os.stat(output_path)[6]
    compress_only_us = total_elapsed_us - read_elapsed_us

    total_s = total_elapsed_us / 1_000_000
    compress_only_s = compress_only_us / 1_000_000
    total_bps = decompressed_len / total_s if total_s > 0 else 0
    compress_bps = decompressed_len / compress_only_s if compress_only_s > 0 else 0

    print(f"{decompressed_len=:,}")
    print(f"{compressed_len=:,}")
    print(f"read_tare={read_elapsed_us / 1_000_000:.6f}s")
    print(f"total_elapsed={total_s:.6f}s")
    print(f"end-to-end={total_bps:,.0f} bytes/s")
    print(f"compression-only={compress_bps:,.0f} bytes/s")


if __name__ == "__main__":
    main()
