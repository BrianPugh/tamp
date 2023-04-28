"""Micropython code to be ran on-device.
"""
import uprofiler

uprofiler.print_period = 0

import tamp


@uprofiler.profile
def main():
    block_size = 1024
    decompressed_len = 0
    compressed_len = 0

    with open("enwik8-100kb", "rb") as input_file, tamp.open("enwik8-100kb.tamp", "wb") as compressed_f:
        while chunk := input_file.read(block_size):
            decompressed_len += len(chunk)
            compressed_len += compressed_f.write(chunk)
        compressed_len += compressed_f.flush(write_token=False)
    print(f"{decompressed_len=:,}")
    print(f"{compressed_len=:,}")


if __name__ == "__main__":
    main()
    uprofiler.print_results()
