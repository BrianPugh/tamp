"""Micropython code to benchmark builtin deflate compression on-device."""

import gc
import io
import time

import deflate


def main():
    # Load input data into RAM before timing
    gc.collect()
    print("Loading input data into RAM...")
    with open("enwik8-100kb", "rb") as f:
        input_data = f.read()
    decompressed_len = len(input_data)

    # Pre-allocate output buffer
    gc.collect()
    output_buffer = io.BytesIO()

    # Time only the compression (RAM to RAM)
    # Using RAW format and wbits=10 (1KB window) to match Tamp benchmarks
    print("Compressing with deflate...")
    start_ms = time.ticks_ms()
    with deflate.DeflateIO(output_buffer, deflate.RAW, 10) as compressor:
        compressor.write(input_data)
    elapsed_ms = time.ticks_diff(time.ticks_ms(), start_ms)

    compressed_data = output_buffer.getvalue()
    compressed_len = len(compressed_data)

    # Write to flash for verification (not timed)
    with open("enwik8-100kb.deflate", "wb") as f:
        f.write(compressed_data)

    elapsed_s = elapsed_ms / 1000
    bytes_per_sec = decompressed_len / elapsed_s if elapsed_s > 0 else 0

    print(f"{decompressed_len=:,}")
    print(f"{compressed_len=:,}")
    print(f"elapsed={elapsed_s:.3f}s")
    print(f"compression={bytes_per_sec:,.0f} bytes/s")


if __name__ == "__main__":
    main()
