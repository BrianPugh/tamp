import pickle
from collections import Counter
from io import BytesIO
from pathlib import Path
from time import monotonic

import matplotlib.pyplot as plt
import numpy as np
from cyclopts import App

from tamp.compressor import Compressor  # Explicitly the python implementation

app = App()

app.command(collect_app := App("collect"))
app.command(analyze_app := App("analyze"))


@collect_app.command(name="rle-opportunities")
def collect_rle_opportunities(
    src: Path,
    dst: Path = Path("out.events"),
    *,
    include_single_length: bool = True,
):
    plaintext = src.read_bytes()

    if not plaintext:
        print("Empty file")
        return

    data = np.frombuffer(plaintext, dtype=np.uint8)

    # Find run boundaries using diff - transitions occur where consecutive bytes differ
    # Pad with a different value to ensure first and last runs are captured
    padded = np.concatenate(([data[0] - 1], data, [data[-1] - 1]))
    transitions = np.diff(padded) != 0

    # Get indices where runs start and end
    run_boundaries = np.where(transitions)[0]

    # Calculate run lengths
    run_lengths = np.diff(run_boundaries)

    # Filter for runs > 1 (actual repetitions)
    rle_runs = run_lengths[run_lengths > int(not include_single_length)]

    # Create histogram using numpy
    if len(rle_runs) > 0:
        unique_lengths, counts = np.unique(rle_runs, return_counts=True)
        sorted_histogram = dict(zip(unique_lengths.tolist(), counts.tolist()))
    else:
        sorted_histogram = {}

    # Save histogram as pickled data for later analysis
    with dst.open("wb") as f:
        pickle.dump(sorted_histogram, f)


@analyze_app.command(name="rle-opportunities")
def analyze_rle_opportunities(
    src: Path,
):
    with src.open("rb") as f:
        histogram_data = pickle.load(f)  # noqa: S301
    plot_histogram(
        histogram_data, x_label="Run Lengths", title=f"{src.stem} Same-Character Run Lengths", log_scale=True
    )


@collect_app.command(name="literal-streaks")
def collect_literal_streaks(
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


@analyze_app.command(name="literal-streaks")
def analyze_literal_streaks(
    src: Path = Path("out.events"),
    *,
    plot: bool = False,
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

    if plot:
        plot_histogram(
            histogram_data,
            x_label="Literal Run Lengths",
            title=f"{src.stem} Literal Run Lengths",
        )


def plot_histogram(histogram_data, *, x_label: str, title: str, log_scale: bool = False):
    """Plot a histogram of run length data using matplotlib.

    Parameters
    ----------
    histogram_data : dict
        Dictionary mapping run lengths (keys) to their frequencies (values)
    x_label : str
        Label for the x-axis
    title : str
        Title for the plot
    log_scale : bool, optional
        If True, use logarithmic scale for y-axis, by default False

    Returns
    -------
    None
        Displays the plot using matplotlib.show()
    """
    if not histogram_data:
        print("No False runs found in data")
        return

    run_lengths = np.array(list(histogram_data.keys()))
    frequencies = np.array(list(histogram_data.values()))

    plt.figure(figsize=(14, 8))

    # Create bar plot
    bars = plt.bar(run_lengths, frequencies, alpha=0.7, edgecolor="black", color="steelblue", linewidth=0.5)

    for bar, freq in zip(bars, frequencies):
        if log_scale:  # noqa: SIM108
            # For log scale, position text slightly above the bar height
            y_pos = bar.get_height() * 1.1
        else:
            # For linear scale, use offset based on max frequency
            y_pos = bar.get_height() + 0.01 * max(frequencies)

        plt.text(
            bar.get_x() + bar.get_width() / 2,
            y_pos,
            str(freq),
            ha="center",
            va="bottom",
            fontsize=12,
            rotation=45,
        )

    plt.xlabel(x_label, fontsize=12)
    plt.ylabel("Frequency", fontsize=12)
    plt.title(title, fontsize=14, fontweight="bold")
    plt.grid(True, alpha=0.3, axis="y")

    if log_scale:
        plt.yscale("log")

    # Smart x-axis tick spacing to prevent overlapping
    ax = plt.gca()
    n_bars = len(run_lengths)
    data_range = max(run_lengths) - min(run_lengths)

    if n_bars <= 20:
        # Show all ticks for small number of bars
        ax.set_xticks(run_lengths)
    else:
        # For many bars, calculate appropriate tick density based on range
        # Use more ticks for larger ranges to maintain good resolution
        if data_range <= 50:
            max_ticks = 15
        elif data_range <= 200:
            max_ticks = 20
        elif data_range <= 500:
            max_ticks = 25
        else:
            max_ticks = 30

        # Calculate step size
        tick_step = max(1, data_range / max_ticks)

        # Round to nice numbers (1, 2, 5, 10, 20, 50, 100, 200, 500, etc.)
        magnitude = 10 ** int(np.log10(tick_step))
        normalized_step = tick_step / magnitude

        if normalized_step <= 1:
            nice_step = magnitude
        elif normalized_step <= 2:
            nice_step = 2 * magnitude
        elif normalized_step <= 5:
            nice_step = 5 * magnitude
        else:
            nice_step = 10 * magnitude

        nice_step = max(1, int(nice_step))  # Ensure step is at least 1

        # Generate tick positions
        start_tick = int(min(run_lengths) // nice_step) * nice_step
        tick_positions = np.arange(start_tick, max(run_lengths) + nice_step, nice_step)

        ax.set_xticks(tick_positions)

    # Rotate labels for better readability when there are many ticks
    if n_bars > 10:
        plt.xticks(rotation=45)

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    app()
