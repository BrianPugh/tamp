from collections import Counter
from collections.abc import Iterable
from pathlib import Path
from typing import Annotated, Optional

from cyclopts import Parameter, validators

import tamp
from tamp._c_build_dictionary import score_and_multi_frag, select_candidates
from tamp._c_build_dictionary import score_substrings as _c_score_substrings
from tamp.compressor import (
    _EXTENDED_MATCH_SYMBOL,
    _LEADING_EXTENDED_MATCH_HUFFMAN_BITS,
    _huffman_bits,
)


def _build_bits_saved_table(
    min_pattern_size: int,
    max_pattern_size: int,
    window_bits: int,
    literal_bits: int,
    extended: bool,
) -> list[float]:
    """Precompute bits saved for each match length.

    Covers both basic matches (using Huffman table) and extended matches.
    Returns a list indexed by (length - min_pattern_size).
    """
    inf = float("inf")
    table = []
    extended_threshold = min_pattern_size + 11 + 1  # min length for extended match encoding

    for length in range(min_pattern_size, max_pattern_size + 1):
        literal_cost = length * (1 + literal_bits)
        i = length - min_pattern_size

        basic_cost = _huffman_bits[i] + window_bits if i < len(_huffman_bits) else inf

        if extended and length >= extended_threshold:
            value = length - min_pattern_size - 11 - 1
            code_index = value >> _LEADING_EXTENDED_MATCH_HUFFMAN_BITS
            if code_index < len(_huffman_bits):
                size_bits = _huffman_bits[code_index] - 1 + _LEADING_EXTENDED_MATCH_HUFFMAN_BITS
                ext_cost = _huffman_bits[_EXTENDED_MATCH_SYMBOL] + size_bits + window_bits
            else:
                ext_cost = inf
        else:
            ext_cost = inf

        match_cost = min(basic_cost, ext_cost)
        bits_saved = literal_cost - match_cost
        table.append(max(0.0, bits_saved))

    return table


def score_substrings(
    corpus: list[bytes],
    min_length: int,
    max_length: int,
    window_bits: int,
    literal_bits: int = 8,
) -> dict[bytes, float]:
    """Score all substrings in a corpus by estimated compression savings."""
    window_size = 1 << window_bits
    return _c_score_substrings(corpus, min_length, max_length, window_size, window_bits, literal_bits, _huffman_bits)


def _compute_positions(
    entries: list[bytes],
    corpus: list[bytes],
    window_bits: int,
) -> dict[bytes, float]:
    """Compute Q3 (75th percentile) position for a small set of entries.

    Only scans the corpus for the specific entries, making this efficient
    even for large corpora since the entry count is small.
    """
    window_size = 1 << window_bits
    all_positions: dict[bytes, list[float]] = {e: [] for e in entries}

    for sample in corpus:
        sample = sample[:window_size]
        for entry in entries:
            start = 0
            while True:
                idx = sample.find(entry, start)
                if idx == -1:
                    break
                all_positions[entry].append((idx + len(entry)) / window_size)
                start = idx + 1

    positions: dict[bytes, float] = {}
    for entry, pos_list in all_positions.items():
        if pos_list:
            pos_list.sort()
            positions[entry] = pos_list[min(int(len(pos_list) * 0.75), len(pos_list) - 1)]
        else:
            positions[entry] = 0.5

    return positions


def _split_fragments(fragments: list[bytes], pattern: bytes, min_length: int) -> list[bytes]:
    """Split all fragments on a pattern, keeping pieces >= min_length."""
    return [part for fragment in fragments for part in fragment.split(pattern) if len(part) >= min_length]


def build_dictionary(
    corpus: Iterable[bytes],
    window_bits: int = 10,
    literal_bits: int = 8,
    extended: bool = True,
    trim_threshold: int = 8,
    target_fill: float = 1.0,
) -> tuple[bytearray, int]:
    """Build an optimal custom dictionary from a corpus of samples.

    Iteratively extracts the most common long substring from the corpus,
    adds it to the dictionary, and splits the corpus on that substring.
    Repeats on the remaining fragments until the budget is reached.

    Parameters
    ----------
    corpus
        Iterable of byte samples (e.g., one per file/message).
        Consumed in a single pass; does not need to fit in memory at once.
    window_bits
        Window size in bits [8-15]. Dictionary = 2^window_bits bytes.
    literal_bits
        Literal bits [5-8].
    extended
        If True, optimize for extended format (longer max match ~225 bytes).
        If False, use non-extended max match (min_pattern_size + 13).
    trim_threshold
        Minimum length for common substring extraction.
    target_fill
        Maximum fraction of dictionary to fill with corpus-derived
        content (0.0-1.0). Actual fill may be less if the corpus lacks
        sufficient common patterns.

    Returns
    -------
    tuple of (dictionary bytearray, effective_size).
    dictionary is exactly 2^window_bits bytes.
    effective_size is how many bytes contain useful corpus-derived content.
    """
    dictionary_size = 1 << window_bits
    budget = int(dictionary_size * max(0.0, min(1.0, target_fill)))
    min_length = tamp.compute_min_pattern_size(window_bits, literal_bits)
    # Cap max_length at the longest efficiently-scorable match.
    # Extended matches (17+) score high in absolute terms but use too
    # much dictionary space per bit saved — basic matches (2-16) are
    # more efficient per byte.
    max_length = min(min_length + len(_huffman_bits) - 1, dictionary_size)

    bits_saved_table = _build_bits_saved_table(min_length, max_length, window_bits, literal_bits, extended)

    # Materialize corpus, truncating to window size. Only the first
    # window_size bytes of each message benefit from the dictionary.
    corpus_list = [s[:dictionary_size] for s in corpus if s]
    if not corpus_list:
        return tamp.initialize_dictionary(dictionary_size, literal=literal_bits), 0

    # Score substrings and identify multi-fragment substrings in one pass.
    scores, multi_frag_content = score_and_multi_frag(
        corpus_list,
        min_length,
        max_length,
        dictionary_size,
        bits_saved_table,
        trim_threshold,
    )
    if not scores:
        return tamp.initialize_dictionary(dictionary_size, literal=literal_bits), 0

    # Phase 1: Extract common long substrings using pre-computed scores.
    fragments = list(corpus_list)

    # Cap: only top candidates matter for a ~1KB dictionary.
    candidates = sorted(
        ((sub, sc) for sub, sc in scores.items() if len(sub) >= trim_threshold),
        key=lambda x: x[1],
        reverse=True,
    )[:50000]

    entries = select_candidates(candidates, multi_frag_content, budget, min_length + 1)
    total_entry_bytes = sum(len(e) for e in entries)
    for entry in entries:
        fragments = _split_fragments(fragments, entry, min_length)

    # Phase 2: Fill remaining space with shorter common substrings.
    # Only add substrings that appear in 2+ fragments (will generalize)
    # and aren't fully contained in existing entries (or vice versa).
    # Unlike Phase 1's strict substring-overlap check, Phase 2 uses
    # simple containment so that short high-frequency patterns like
    # " to " can be added even if they appear inside a longer Phase 1
    # entry — they match the many corpus positions where the longer
    # context isn't present.
    if total_entry_bytes < budget and fragments:
        frag_scores, frag_multi = score_and_multi_frag(
            fragments,
            min_length,
            max_length,
            dictionary_size,
            bits_saved_table,
            min_length,
        )
        ranked = sorted(frag_scores.items(), key=lambda x: x[1], reverse=True)
        entry_set = set(entries)
        for substring, _score in ranked:
            if substring not in frag_multi:
                continue
            if substring in entry_set:
                continue
            entries.append(substring)
            entry_set.add(substring)
            total_entry_bytes += len(substring)
            if total_entry_bytes >= budget:
                break

    # Deduplicate: extract shared substrings of >= trim_threshold bytes
    # across entries, replacing containing entries with unique remainders.
    # Bounded to prevent pathological cases from looping indefinitely.
    deduplicated = list(entries)
    for _dedup_iter in range(len(entries)):
        sub_counts: Counter[bytes] = Counter()
        for entry in deduplicated:
            seen: set[bytes] = set()
            for length in range(trim_threshold, len(entry)):
                for start in range(len(entry) - length + 1):
                    sub = entry[start : start + length]
                    if sub != entry and sub not in seen:
                        seen.add(sub)
                        sub_counts[sub] += 1

        best_shared = None
        best_key = (0, 0)
        for sub, count in sub_counts.items():
            if count < 2:
                continue
            key = (len(sub), count)
            if key > best_key:
                best_shared = sub
                best_key = key

        if best_shared is None:
            break

        new_entries: list[bytes] = []
        shared_added = False
        for entry in deduplicated:
            if best_shared in entry and best_shared != entry:
                idx = entry.index(best_shared)
                prefix = entry[:idx]
                suffix = entry[idx + len(best_shared) :]
                for part in (prefix, suffix):
                    if len(part) >= trim_threshold:
                        new_entries.append(part)
                if not shared_added:
                    new_entries.append(best_shared)
                    shared_added = True
            else:
                new_entries.append(entry)
        deduplicated = new_entries

    # Remove entries fully contained in other entries
    deduplicated = [e for e in deduplicated if not any(e in other and e != other for other in deduplicated)]

    # Phase 3: Backfill remaining space after deduplication freed space.
    # Use substring-overlap check (same threshold as Phase 1) to prevent
    # shifted duplicates from filling the dictionary.
    overlap_len = min_length + 1
    covered_subs: set[bytes] = set()
    for entry in deduplicated:
        for k in range(len(entry) - overlap_len + 1):
            covered_subs.add(entry[k : k + overlap_len])

    deduplicated_set = set(deduplicated)
    deduplicated_bytes = sum(len(e) for e in deduplicated)
    if deduplicated_bytes < budget:
        backfill_ranked = sorted(scores.items(), key=lambda x: x[1], reverse=True)
        for substring, sc in backfill_ranked:
            if sc <= 0:
                continue
            if substring in deduplicated_set:
                continue
            # Check for overlap with existing entries.
            has_overlap = False
            for k in range(len(substring) - overlap_len + 1):
                if substring[k : k + overlap_len] in covered_subs:
                    has_overlap = True
                    break
            if has_overlap:
                continue
            deduplicated.append(substring)
            deduplicated_set.add(substring)
            for k in range(len(substring) - overlap_len + 1):
                covered_subs.add(substring[k : k + overlap_len])
            deduplicated_bytes += len(substring)
            if deduplicated_bytes >= budget:
                break

    # Compute positions only for the final small set of entries.
    positions = _compute_positions(deduplicated, corpus_list, window_bits)

    scored_entries = [(entry, scores.get(entry, 0.0), positions.get(entry, 0.5)) for entry in deduplicated]
    return pack_dictionary(scored_entries, window_bits, literal_bits)


def evaluate_dictionary_tradeoff(
    corpus: list[bytes],
    full_dictionary: bytearray,
    effective_size: int,
    window_bits: int,
    literal_bits: int = 8,
    extended: bool = True,
    n_points: int = 20,
) -> list[tuple[int, int]]:
    """Evaluate compression at various dictionary sizes.

    Since pack_dictionary places entries contiguously from the right end
    in value order, the rightmost S bytes always contain the S most
    valuable bytes. Sub-dictionaries are constructed by copying just
    those bytes onto an initialize_dictionary base.

    Returns
    -------
    list of (dict_effective_bytes, total_compressed_bytes).
    Always includes effective_size.
    """
    dictionary_size = 1 << window_bits
    default_dict = tamp.initialize_dictionary(dictionary_size, literal=literal_bits)
    header_bytes = len(corpus)  # 1 header byte per sample to subtract

    def _compress_total(d: bytearray) -> int:
        total = 0
        for sample in corpus:
            total += len(
                tamp.compress(
                    sample, window=window_bits, literal=literal_bits, dictionary=bytearray(d), extended=extended
                )
            )
        return total - header_bytes

    if effective_size == 0:
        return [(0, _compress_total(default_dict))]

    step = max(1, effective_size // n_points)
    sizes = list(range(step, effective_size, step))
    if not sizes or sizes[-1] != effective_size:
        sizes.append(effective_size)

    results = []
    for size in sizes:
        d = bytearray(default_dict)
        d[dictionary_size - size :] = full_dictionary[dictionary_size - size :]
        results.append((size, _compress_total(d)))

    return results


def find_knee(
    results: list[tuple[int, int]],
    min_benefit: float = 0.75,
    linearity_threshold: float = 0.10,
) -> int:
    """Find the knee of the compression-vs-size curve.

    Uses the Kneedle method: in normalized space, find the point with
    maximum perpendicular distance from the line connecting the first
    and last points.

    Handles three curve shapes:
    - Sharp knee (e.g., green-eggs): Kneedle point is used directly.
    - Gradual curve (e.g., tweets): Kneedle point may be too early;
      bumped up to the first point reaching ``min_benefit``.
    - Nearly linear (e.g., SMS): no meaningful knee exists; every byte
      of dictionary provides roughly equal value. Returns full size.

    Returns the dict_effective_bytes value at the knee.
    """
    if len(results) <= 2:
        return results[-1][0]

    xs = [r[0] for r in results]
    ys = [r[1] for r in results]

    x_min, x_max = xs[0], xs[-1]
    y_min, y_max = min(ys), max(ys)

    if x_max == x_min or y_max == y_min:
        return results[-1][0]

    # Kneedle: maximum perpendicular distance from diagonal.
    xn = [(x - x_min) / (x_max - x_min) for x in xs]
    yn = [(y - y_min) / (y_max - y_min) for y in ys]

    x1, y1 = xn[0], yn[0]
    x2, y2 = xn[-1], yn[-1]
    line_len = ((x2 - x1) ** 2 + (y2 - y1) ** 2) ** 0.5

    best_dist = -1.0
    knee_idx = 0
    for i in range(1, len(results) - 1):
        dist = abs((y2 - y1) * xn[i] - (x2 - x1) * yn[i] + x2 * y1 - y2 * x1) / line_len
        if dist > best_dist:
            best_dist = dist
            knee_idx = i

    # If the curve is nearly linear (max distance from diagonal is small),
    # there's no meaningful knee — every byte provides similar value.
    # Use the full dictionary.
    if best_dist < linearity_threshold:
        return results[-1][0]

    # Ensure the knee captures at least min_benefit of the total improvement.
    # For gradual curves (like tweets), the Kneedle point may be too early.
    improvement_range = y_max - y_min
    if improvement_range > 0:
        knee_benefit = (y_max - ys[knee_idx]) / improvement_range
        if knee_benefit < min_benefit:
            for i, y in enumerate(ys):
                if (y_max - y) / improvement_range >= min_benefit:
                    knee_idx = i
                    break

    return results[knee_idx][0]


def pack_dictionary(
    scored_entries: list[tuple[bytes, float, float]],
    window_bits: int,
    literal_bits: int,
) -> tuple[bytearray, int]:
    """Pack entries into a dictionary buffer, right-to-left by position and density.

    Entries that appear late in messages go to the right end (survive
    longest). Among entries at similar positions, higher score density
    (bits saved per byte) goes further right.

    Parameters
    ----------
    scored_entries
        List of (entry, score, position) tuples.
        score: bits saved across the corpus.
        position: Q3 (75th percentile) position in messages where this
            entry appears (0.0 = start, 1.0 = end).
    window_bits
        Window size in bits. Dictionary size = 2^window_bits.
    literal_bits
        Literal bits, used to initialize unused space with the default pattern.

    Returns
    -------
    tuple of (dictionary bytearray, effective_size in bytes).
    effective_size is how many bytes of the dictionary contain useful content.
    """
    dictionary_size = 1 << window_bits

    def sort_key(item: tuple[bytes, float, float]) -> tuple[float, float]:
        entry, score, pos = item
        return (pos, score / len(entry))

    # Sort ascending: leftmost = early-appearing/low-density,
    # rightmost = late-appearing/high-density.
    ranked = sorted(scored_entries, key=sort_key)

    packed_entries: list[bytes] = []
    used = 0
    for entry, score, _pos in reversed(ranked):
        if used + len(entry) > dictionary_size:
            continue
        if score <= 0:
            continue
        packed_entries.append(entry)
        used += len(entry)

    result = tamp.initialize_dictionary(dictionary_size, literal=literal_bits)
    pos = dictionary_size
    for entry in packed_entries:
        pos -= len(entry)
        result[pos : pos + len(entry)] = entry

    return result, used


def _read_corpus(input_path: Path, delimiter: Optional[str]) -> Iterable[bytes]:
    """Yield corpus samples from a directory or a delimited file.

    Parameters
    ----------
    input_path
        Directory of files (one sample per file) or a single file.
    delimiter
        If input_path is a file, split on this delimiter.
        Ignored when input_path is a directory.
    """
    if input_path.is_dir():
        files = sorted(p for p in input_path.iterdir() if p.is_file())
        if not files:
            raise ValueError(f"No files found in {input_path}")
        for f in files:
            data = f.read_bytes()
            if data:
                yield data
    elif input_path.is_file():
        if delimiter is None:
            delimiter = "\n"
        content = input_path.read_bytes()
        for chunk in content.split(delimiter.encode()):
            if chunk:
                yield chunk
    else:
        raise ValueError(f"Input path does not exist: {input_path}")


def build_dictionary_cli(
    input: Path,
    output: Annotated[Path, Parameter(name=["--output", "-o"])] = Path("dictionary.bin"),
    *,
    window: Annotated[
        int,
        Parameter(
            name=["--window", "-w"],
            validator=validators.Number(gte=8, lte=15),
        ),
    ] = 10,
    literal: Annotated[
        int,
        Parameter(
            name=["--literal", "-l"],
            validator=validators.Number(gte=5, lte=8),
        ),
    ] = 8,
    extended: bool = True,
    delimiter: Optional[str] = "\n",
    trim_threshold: Annotated[
        int,
        Parameter(
            name=["--trim-threshold", "-t"],
            validator=validators.Number(gte=2),
        ),
    ] = 8,
    target_fill: Annotated[
        Optional[float],
        Parameter(
            name=["--target-fill", "-f"],
            validator=validators.Number(gt=0.0, lte=1.0),
        ),
    ] = None,
    quiet: Annotated[bool, Parameter(name=["--quiet", "-q"])] = False,
):
    """Build an optimal custom dictionary from a corpus.

    Input can be a directory of files (one sample per file) or a single file
    split by a delimiter (default: newline). Empty samples are skipped.

    Outputs only the effective dictionary bytes. The compress/decompress
    commands automatically expand undersized dictionary files with
    initialize_dictionary.

    Prints a compression-vs-size tradeoff table and automatically
    selects the dictionary size at the knee of the curve (where
    diminishing returns begin).

    The exact dictionary output may change between releases as the
    selection algorithm is improved. Pin a specific version of tamp
    if reproducibility is required.

    Parameters
    ----------
    input: Path
        Directory of corpus files or a single delimited file.
    output: Path
        Output path for the binary dictionary file.
    window: int
        Number of window bits [8-15]. Dictionary size = 2^window bytes.
    literal: int
        Number of literal bits [5-8].
    extended: bool
        Optimize for extended compression format (longer match lengths).
    delimiter: Optional[str]
        Delimiter for splitting a single file into samples.
        Ignored when input is a directory.
    trim_threshold: int
        Minimum length of a common substring to extract. Lower values produce
        more diverse dictionaries; higher values allow more full-phrase entries.
    target_fill: Optional[float]
        Fraction of dictionary to fill (0.0-1.0). If not specified,
        automatically selects the knee of the tradeoff curve.
    quiet: bool
        Suppress the tradeoff table and summary output.
    """
    dictionary_size = 1 << window

    # Materialize corpus for reuse in compression analysis.
    corpus_list = list(_read_corpus(input, delimiter))

    # Build at full fill for analysis.
    dictionary, effective_size = build_dictionary(
        list(corpus_list),
        window_bits=window,
        literal_bits=literal,
        extended=extended,
        trim_threshold=trim_threshold,
        target_fill=1.0,
    )

    # Evaluate tradeoff at various fill levels.
    results = evaluate_dictionary_tradeoff(
        corpus_list,
        dictionary,
        effective_size,
        window_bits=window,
        literal_bits=literal,
        extended=extended,
    )

    corpus_total = sum(len(s) for s in corpus_list)
    # Baseline: compress with no custom dictionary (subtract 1-byte header per message).
    baseline_compressed = sum(
        len(tamp.compress(s, window=window, literal=literal, extended=extended)) - 1 for s in corpus_list
    )
    knee_size = find_knee(results)
    best_compressed = results[-1][1]
    improvement_range = baseline_compressed - best_compressed

    if target_fill is not None:
        target_bytes = int(dictionary_size * target_fill)
        highlight_size = min((db for db, _ in results), key=lambda db: abs(db - target_bytes))
    else:
        highlight_size = knee_size

    if not quiet:
        from rich.console import Console
        from rich.table import Table
        from rich.text import Text

        console = Console(stderr=True, width=80)
        table = Table(show_header=True, show_edge=False, pad_edge=False, box=None)
        table.add_column("Fill", justify="right", style="dim", width=4)
        table.add_column("Bytes", justify="right", width=5)
        table.add_column("Compressed", justify="right", width=10)
        table.add_column("Ratio", justify="right", width=5)
        bar_width = 43
        table.add_column("Benefit", no_wrap=True)

        console.print(f"\nDictionary analysis (window={window}, {dictionary_size} bytes):")
        console.print(f"Corpus: {len(corpus_list)} samples, {corpus_total} bytes total\n")

        seen_fills: set[str] = set()
        for dict_bytes, compressed_bytes in results:
            ratio = corpus_total / compressed_bytes if compressed_bytes > 0 else float("inf")
            benefit = (
                (baseline_compressed - compressed_bytes) / improvement_range * 100 if improvement_range > 0 else 100.0
            )
            fill_frac = dict_bytes / dictionary_size
            fill_str = f"{fill_frac:.2f}"
            if fill_str in seen_fills:
                continue
            seen_fills.add(fill_str)

            is_selected = dict_bytes == highlight_size
            n_filled = int(benefit / 100 * bar_width + 0.5)
            bar_text = Text()
            bar_text.append(f"{benefit:3.0f}% ", style="bold" if is_selected else "")
            bar_color = "bright_green" if is_selected else "green"
            bar_text.append("\u2588" * n_filled, style=bar_color)

            table.add_row(
                fill_str,
                str(dict_bytes),
                str(compressed_bytes),
                f"{ratio:.2f}x",
                bar_text,
                style="bold" if is_selected else "",
            )

        console.print(table)

    # Rebuild at the desired fill. A fresh build at lower fill produces
    # better entry selection than truncating the full dictionary (measured
    # 22% better compression on green-eggs-and-ham).
    desired_fill = target_fill if target_fill is not None else knee_size / dictionary_size
    if desired_fill < 1.0:
        dictionary, effective_size = build_dictionary(
            list(corpus_list),
            window_bits=window,
            literal_bits=literal,
            extended=extended,
            trim_threshold=trim_threshold,
            target_fill=desired_fill,
        )

    output.write_bytes(dictionary[dictionary_size - effective_size :])
    output_size = effective_size

    if not quiet:
        # Measure actual compression with the output dictionary.
        output_compressed = sum(
            len(tamp.compress(s, window=window, literal=literal, dictionary=bytearray(dictionary), extended=extended))
            - 1
            for s in corpus_list
        )
        baseline_ratio = corpus_total / baseline_compressed if baseline_compressed > 0 else float("inf")
        output_ratio = corpus_total / output_compressed if output_compressed > 0 else float("inf")

        savings = baseline_compressed - output_compressed
        savings_pct = savings / baseline_compressed * 100 if baseline_compressed > 0 else 0
        console.print(f"\nDict size:  {output_size} bytes")
        console.print(f"No dict:    {baseline_compressed} bytes compressed ({baseline_ratio:.2f}x)")
        console.print(
            f"With dict:  {output_compressed} bytes compressed ({output_ratio:.2f}x, -{savings_pct:.1f}% vs no dict)"
        )
