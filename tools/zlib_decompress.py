import sys
import zlib


def main():
    compressed_data = sys.stdin.buffer.read()
    zlib.decompress(compressed_data)


if __name__ == "__main__":
    main()
