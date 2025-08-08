from collections import Counter
from io import BytesIO
from pathlib import Path
from time import monotonic

import matplotlib.pyplot as plt
import numpy as np
from cyclopts import App

from tamp.compressor import Compressor  # Explicitly the python implementation

app = App()


@app.command
def collect(
    src: Path,
    dst: Path = Path("out.events"),
    window: int = 10,
):
    plaintext = src.read_bytes()

    """
    MSb - (1) if token, (0) if literal
    lower 7 bits - match size
    """
    events = bytearray(len(plaintext))
    n_events = 0

    def token_cb(offset, match_size, pattern):
        nonlocal n_events
        events[n_events] = 0x80 | match_size
        n_events += 1

    def literal_cb(char):
        nonlocal n_events
        n_events += 1

    def flush_cb():
        pass

    with BytesIO() as compressed_out:
        compressor = Compressor(compressed_out, window=window)

        compressor.token_cb = token_cb
        compressor.literal_cb = literal_cb
        compressor.flush_cb = flush_cb

        t_start = monotonic()
        compressor.write(plaintext)
        compressor.flush(write_token=False)
        t_elapsed = monotonic() - t_start

        compressed_size = compressed_out.tell()

    events = events[:n_events]
    print(f"{n_events=}")
    print(f"{compressed_size=}")
    print(f"{t_elapsed=}")

    dst.write_bytes(events)


@app.command
def analyze(
    src: Path = Path("out.events"),
):
    data = src.read_bytes()

    uint8_array = np.frombuffer(data, dtype=np.uint8)

    # Convert that array into a boolean array based on the MSB value
    # MSB is 1 if value >= 128, 0 if value < 128
    is_pattern = uint8_array >= 0x80
    padded = np.concatenate(([True], is_pattern, [True]))
    transitions = np.diff(padded.astype(np.int8))

    false_starts = np.where(transitions == -1)[0]
    false_ends = np.where(transitions == 1)[0]

    # Calculate run lengths
    false_run_lengths = false_ends - false_starts

    # Generate histogram data
    if len(false_run_lengths) > 0:
        histogram = Counter(false_run_lengths)
        histogram_data = dict(sorted(histogram.items()))
    else:
        histogram_data = {}
    plot_histogram(histogram_data, "Literal Run Lengths.")


def plot_histogram(histogram_data, x_label: str):
    if not histogram_data:
        print("No False runs found in data")
        return

    run_lengths = np.array(list(histogram_data.keys()))
    frequencies = np.array(list(histogram_data.values()))

    plt.figure(figsize=(14, 8))

    # Create bar plot
    bars = plt.bar(run_lengths, frequencies, alpha=0.7, edgecolor="black", color="steelblue", linewidth=0.5)

    for bar, freq in zip(bars, frequencies):
        plt.text(
            bar.get_x() + bar.get_width() / 2,
            bar.get_height() + 0.01 * max(frequencies),
            str(freq),
            ha="center",
            va="bottom",
            fontsize=8,
        )

    plt.xlabel(x_label, fontsize=12)
    plt.ylabel("Frequency", fontsize=12)
    plt.title(x_label, fontsize=14, fontweight="bold")
    plt.grid(True, alpha=0.3, axis="y")

    # Add statistics text
    total_runs = sum(frequencies)
    mean_length = np.average(run_lengths, weights=frequencies)
    max_length = max(run_lengths)

    stats_text = f"Total False Runs: {total_runs}\nMean Length: {mean_length:.2f}\nMax Length: {max_length}"
    plt.text(
        0.02,
        0.98,
        stats_text,
        transform=plt.gca().transAxes,
        verticalalignment="top",
        horizontalalignment="right",
        bbox={"boxstyle": "round", "facecolor": "wheat", "alpha": 0.8},
    )

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    app()
