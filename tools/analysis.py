import argparse
import pickle
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from huffman import huffman_coding


def _generate_ticks(data, num_ticks=20):
    step_size = (len(data) - 1) / (num_ticks - 1)
    xticks_positions = [round(i * step_size) for i in range(num_ticks)]
    xticks_labels = [str(int(pos)) for pos in xticks_positions]
    return (xticks_positions, xticks_labels)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("window_bits", type=int)
    parser.add_argument("--plot", action="store_true")
    parser.add_argument("--pdb", action="store_true")
    args = parser.parse_args()

    with Path(f"build/results-w{args.window_bits}.pkl").open("rb") as f:
        r = pickle.load(f)  # noqa: S301
        for k, v in r.items():
            if isinstance(v, list):
                r[k] = np.array(v)

    r["token_sizes"] = r["token_sizes"] / r["token_sizes"].sum()
    r["token_distances"] = r["token_distances"] / r["token_distances"].sum()

    for k, v in r.items():
        if isinstance(v, np.ndarray):
            continue
        print(f"{k}: {v:,}")

    literal_prob = r["n_literals"] / (r["n_literals"] + r["n_tokens"])
    print(f"{literal_prob=}")

    min_pattern_size = r["token_sizes"].nonzero()[0].min()
    max_pattern_size = r["token_sizes"].nonzero()[0].max()

    probs = {i: r["token_sizes"][i] for i in range(min_pattern_size, max_pattern_size + 1)}

    huffman_codes = huffman_coding(probs)

    bits_per_symbols = {}
    shortest_symbol_size = 100
    for k, v in sorted(huffman_codes.items()):
        symbol_size = len(v)
        bits_per_symbols[k] = symbol_size
        if symbol_size < shortest_symbol_size:
            shortest_symbol_size = symbol_size

    # if huffman_codes[shortest_symbol] == "1":
    #    # Invert all codes;
    #    for symbol, code in huffman_codes.items():
    #        huffman_codes[symbol] = code.replace("0","Z").replace("1", "0").replace("Z", "1")

    average_bits_per_symbol = 0
    for code in bits_per_symbols:
        average_bits_per_symbol += bits_per_symbols[code] * probs[code]

    print(f"Huffman pattern size code: {average_bits_per_symbol=}")

    print("Huffman codes:")
    huffman_code_array = b""
    huffman_bit_size_array = []
    for char, code in sorted(huffman_codes.items()):
        print(f"{char}: {code}")
        huffman_code_array += int(code, 2).to_bytes(1, "little")
        huffman_bit_size_array.append(len(code) + 1)  # plus 1 for implicit is_literal flag.
    huffman_bit_size_array = tuple(huffman_bit_size_array)
    print(f"{huffman_bit_size_array=}")
    print(f"{huffman_code_array=}")

    offset_weighted_average = np.dot(r["token_distances"], np.arange(len(r["token_distances"])))
    print(f"50% of offsets are under {offset_weighted_average}.")

    if args.pdb:
        breakpoint()

    if args.plot:
        plt.rc("font", size=30)

        plt.subplot(1, 2, 1)
        data = r["token_distances"]
        indices = range(len(data))
        plt.bar(indices, data, width=1)
        plt.xticks(*_generate_ticks(data))
        plt.xlabel("Offset")
        plt.ylabel("Occurrence")
        plt.title(f"Token Distances (w={args.window_bits})")

        plt.subplot(1, 2, 2)
        indices = np.array(sorted(huffman_codes.keys()))
        data = [probs[x] for x in indices]
        plt.bar(indices, data, width=1, align="center")
        plt.yticks(np.arange(0, 0.55, 0.05))
        plt.xticks(
            indices,
            indices,
        )
        plt.xlim(0, max(indices))
        plt.xlabel("Match Size")
        plt.ylabel("Occurrence")
        plt.title(f"Match Sizes (enwik8, w={args.window_bits})")

        plt.grid(True)

        plt.show()


if __name__ == "__main__":
    main()
