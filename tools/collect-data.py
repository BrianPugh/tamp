import argparse
import pickle
import time
from io import BytesIO
from pathlib import Path

from tamp.compressor import Compressor


def timeit(func):
    def wrapper(*args, **kwargs):
        start = time.time()
        result = func(*args, **kwargs)
        end = time.time()
        print(f"Function {func.__name__} took {end - start:.5f} seconds to execute.")
        return result

    return wrapper


@timeit
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("window_bits", type=int)
    args = parser.parse_args()

    window_size = 1 << args.window_bits

    decompressed = Path("datasets/enwik8").read_bytes()

    results = {
        "n_literals": 0,
        "n_tokens": 0,
        "n_flushes": 0,
        "token_distances": [0] * window_size,
        "token_sizes": [0] * 20,
        "decompressed_size": 0,
        "compressed_size": 0,
        "ratio": 0,
    }

    def token_cb(offset, match_size, string):
        results["n_tokens"] += 1
        results["token_distances"][offset] += 1
        results["token_sizes"][match_size] += 1

    def literal_cb(char):
        results["n_literals"] += 1

    def flush_cb():
        results["n_flushes"] += 1

    with BytesIO() as compressed_out:
        compressor = Compressor(
            compressed_out,
            window=args.window_bits,
        )
        compressor.token_cb = token_cb
        compressor.literal_cb = literal_cb
        compressor.flush_cb = flush_cb

        compressor.write(decompressed)
        compressor.flush()

        results["decompressed_size"] = len(decompressed)
        results["compressed_size"] = compressed_out.tell()
        results["ratio"] = results["decompressed_size"] / results["compressed_size"]

    with Path(f"build/results-w{args.window_bits}.pkl").open("wb") as f:
        pickle.dump(results, f)


if __name__ == "__main__":
    main()
