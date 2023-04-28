"""Micropython code to be ran on-device.
"""
import uprofiler

uprofiler.print_period = 0

import tamp


@uprofiler.profile
def main():
    block_size = 1024
    decompressed_len = 0

    with tamp.open("enwik8-100kb.tamp", "rb") as input_file, open("enwik8-100kb-decompressed", "wb") as output_file:
        while True:
            chunk = input_file.read(block_size)
            decompressed_len += len(chunk)
            output_file.write(chunk)
            if len(chunk) < block_size:
                break
    print(f"{decompressed_len=:,}")


if __name__ == "__main__":
    main()
    uprofiler.print_results()
