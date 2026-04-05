# cython: language_level=3, boundscheck=False, wraparound=False
"""Cython-accelerated inner loops for dictionary generation."""

from cpython.exc cimport PyErr_CheckSignals


def score_substrings(
    list corpus,
    int min_length,
    int max_length,
    int window_size,
    int window_bits,
    int literal_bits,
    bytes huffman_bits,
):
    """Count all substring occurrences and return scores dict.

    Combines counting and scoring into a single Cython pass.
    Returns dict mapping substring -> total bits saved across corpus.
    """
    cdef dict counts = {}
    cdef bytes sample, sub
    cdef int sample_len, length, start, count
    cdef int capped_max

    for sample in corpus:
        PyErr_CheckSignals()
        if len(sample) > window_size:
            sample = sample[:window_size]
        sample_len = len(sample)
        if sample_len == 0:
            continue
        capped_max = min(max_length + 1, sample_len + 1)
        for length in range(min_length, capped_max):
            for start in range(sample_len - length + 1):
                sub = sample[start : start + length]
                if sub in counts:
                    counts[sub] += 1
                else:
                    counts[sub] = 1

    if not counts:
        return {}

    cdef dict scores = {}
    cdef int match_len, i
    cdef double literal_cost, match_cost, bits_saved
    cdef int huff_len = len(huffman_bits)
    cdef const unsigned char *huff = huffman_bits

    for sub, count in counts.items():
        match_len = len(sub)
        literal_cost = match_len * (1 + literal_bits)
        i = match_len - min_length
        if i < 0 or i >= huff_len:
            continue
        match_cost = huff[i] + window_bits
        bits_saved = literal_cost - match_cost
        if bits_saved > 0:
            scores[sub] = count * bits_saved

    return scores


def score_and_multi_frag(
    list corpus,
    int min_length,
    int max_length,
    int window_size,
    list bits_saved_table,
    int multi_frag_min_length,
):
    """Score substrings and identify multi-fragment substrings.

    Uses bottom-up pruning: a substring of length L can only appear in
    2+ samples if its length-(L-1) prefix does too. Starts at min_length,
    keeps only frequent prefixes, and extends incrementally. This avoids
    enumerating the vast majority of long unique substrings.

    Parameters
    ----------
    bits_saved_table
        Precomputed list of bits saved for each match length.
        Indexed by (length - min_length). Covers both basic and
        extended match encodings.

    Returns (scores_dict, multi_frag_set).
    """
    cdef list samples = []
    cdef bytes s
    for s in corpus:
        if len(s) > window_size:
            s = s[:window_size]
        if len(s) > 0:
            samples.append(s)

    if not samples:
        return {}, set()

    cdef dict scores = {}
    cdef set multi_frag = set()
    cdef int table_len = len(bits_saved_table)

    cdef dict sample_counts
    cdef set sample_subs
    cdef set freq
    cdef bytes sample, sub, prefix
    cdef int sample_len, start, length, sc, i
    cdef double bits_saved

    # Bootstrap: enumerate all substrings at min_length.
    # Track per-sample counts (not total occurrences) for scoring.
    sample_counts = {}
    for sample in samples:
        PyErr_CheckSignals()
        sample_len = len(sample)
        sample_subs = set()
        for start in range(sample_len - min_length + 1):
            sample_subs.add(sample[start : start + min_length])
        for sub in sample_subs:
            if sub in sample_counts:
                sample_counts[sub] += 1
            else:
                sample_counts[sub] = 1

    # Score and collect frequent prefixes.
    freq = set()
    bits_saved = bits_saved_table[0] if table_len > 0 else 0
    if bits_saved > 0:
        for sub, sc in sample_counts.items():
            if sc >= 2:
                scores[sub] = sc * bits_saved
                freq.add(sub)
                if min_length >= multi_frag_min_length:
                    multi_frag.add(sub)
    else:
        for sub, sc in sample_counts.items():
            if sc >= 2:
                freq.add(sub)

    # Extend length by length, pruning by frequent prefixes.
    for length in range(min_length + 1, max_length + 1):
        if not freq:
            break

        sample_counts = {}
        for sample in samples:
            PyErr_CheckSignals()
            sample_len = len(sample)
            if sample_len < length:
                continue
            sample_subs = set()
            for start in range(sample_len - length + 1):
                prefix = sample[start : start + length - 1]
                if prefix not in freq:
                    continue
                sample_subs.add(sample[start : start + length])
            for sub in sample_subs:
                if sub in sample_counts:
                    sample_counts[sub] += 1
                else:
                    sample_counts[sub] = 1

        # Score this length and build new frequent set.
        freq = set()
        i = length - min_length
        bits_saved = bits_saved_table[i] if i < table_len else 0
        for sub, sc in sample_counts.items():
            if sc >= 2:
                if bits_saved > 0:
                    scores[sub] = sc * bits_saved
                if length >= multi_frag_min_length:
                    multi_frag.add(sub)
                freq.add(sub)

    return scores, multi_frag


def select_candidates(
    list candidates,
    set multi_frag_content,
    int budget_remaining,
    int overlap_threshold,
):
    """Extract all valid entries from candidates until budget is exhausted.

    Iterates through candidates (sorted by score descending). For each
    valid candidate (exists in multi_frag_content, i.e. appears in 2+
    fragments), accepts it and removes all candidates that share
    content of >= overlap_threshold bytes. This prevents shifted
    duplicates like "I DO NOT LIKE " and " DO NOT LIKE THE" from
    both being selected.

    Returns a list of accepted entry bytes.
    """
    cdef list result = []
    cdef bytes accepted
    cdef bytes candidate
    cdef bytes sub
    cdef int i, k, n, used
    cdef int write_idx
    cdef int accepted_len
    cdef bint has_overlap

    # Track all overlap_threshold-length substrings of accepted entries.
    cdef set used_subs = set()

    # Pre-filter: only keep candidates present in multi_frag_content.
    cdef list filtered = []
    for i in range(len(candidates)):
        candidate = <bytes>(<tuple>candidates[i])[0]
        if candidate in multi_frag_content:
            filtered.append(candidates[i])

    used = 0
    n = len(filtered)

    while n > 0 and used < budget_remaining:
        PyErr_CheckSignals()
        # Find the first candidate that doesn't overlap with accepted entries.
        accepted = None
        for i in range(n):
            candidate = <bytes>(<tuple>filtered[i])[0]
            has_overlap = False
            for k in range(len(candidate) - overlap_threshold + 1):
                if candidate[k : k + overlap_threshold] in used_subs:
                    has_overlap = True
                    break
            if has_overlap:
                filtered[i] = None
                continue
            accepted = candidate
            filtered[i] = None
            break

        if accepted is None:
            break

        result.append(accepted)
        used += len(accepted)
        accepted_len = len(accepted)

        # Register all overlap_threshold-length substrings of the accepted entry.
        for k in range(accepted_len - overlap_threshold + 1):
            used_subs.add(accepted[k : k + overlap_threshold])

        # Compact the list (remove None entries).
        write_idx = 0
        for i in range(n):
            if filtered[i] is not None:
                filtered[write_idx] = filtered[i]
                write_idx += 1
        del filtered[write_idx:]
        n = write_idx

    return result
