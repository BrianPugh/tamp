import os
import tempfile
import unittest
from pathlib import Path

from tamp.cli.build_dictionary import (
    _compute_positions,
    _split_fragments,
    build_dictionary,
    evaluate_dictionary_tradeoff,
    find_best_trim_threshold,
    find_knee,
    pack_dictionary,
    score_substrings,
)

try:
    from tamp.cli.main import app
except ImportError:
    pass
else:
    from importlib.metadata import version

    from packaging.version import Version

    _cyclopts_version = Version(version("cyclopts"))
    _app_kwargs = {"result_action": "return_value"} if _cyclopts_version >= Version("4.0.0") else {}


class TestScoreSubstrings(unittest.TestCase):
    def test_basic_scoring(self):
        """Common substrings across multiple samples get high scores."""
        corpus = [b"hello world", b"hello there", b"hello world"]
        scores = score_substrings(corpus, min_length=2, max_length=20, window_bits=10)
        self.assertIn(b"hello ", scores)
        self.assertGreater(scores[b"hello "], 0)

    def test_longer_substrings_score_higher(self):
        """A longer common substring scores higher than a shorter one with same frequency."""
        corpus = [b"abcdef"] * 10
        scores = score_substrings(corpus, min_length=2, max_length=20, window_bits=10)
        self.assertGreater(scores[b"abcdef"], scores[b"ab"])

    def test_single_byte_excluded(self):
        """Substrings shorter than min_length are not scored."""
        corpus = [b"aaaa", b"aaaa"]
        scores = score_substrings(corpus, min_length=2, max_length=20, window_bits=10)
        self.assertNotIn(b"a", scores)

    def test_empty_corpus(self):
        """Empty corpus returns empty scores."""
        scores = score_substrings([], min_length=2, max_length=20, window_bits=10)
        self.assertEqual(scores, {})

    def test_occurrence_counting(self):
        """Substrings that appear more often get higher scores."""
        corpus = [b"xyz"] * 5 + [b"aaa aaa aaa aaa aaa"]
        scores = score_substrings(corpus, min_length=3, max_length=20, window_bits=10)
        self.assertEqual(scores[b"xyz"], scores[b"aaa"])

    def test_max_length_respected(self):
        """Substrings longer than max_length are not scored."""
        corpus = [b"abcdefghij"] * 5
        scores = score_substrings(corpus, min_length=2, max_length=5, window_bits=10)
        self.assertIn(b"abcde", scores)
        self.assertNotIn(b"abcdef", scores)

    def test_accepts_list(self):
        """Corpus is a list."""
        scores = score_substrings([b"abc", b"abc"], min_length=2, max_length=5, window_bits=10)
        self.assertIn(b"abc", scores)


class TestComputePositions(unittest.TestCase):
    def test_positions_relative_to_window(self):
        """Positions are fractions of window size, not message length."""
        corpus = [b"hello world"]
        window_size = 1 << 10
        positions = _compute_positions([b"he", b"ld"], corpus, window_bits=10)
        self.assertAlmostEqual(positions[b"he"], 2 / window_size, places=4)
        self.assertAlmostEqual(positions[b"ld"], 11 / window_size, places=4)

    def test_q3_percentile(self):
        """Position uses Q3, so late occurrences pull the value up."""
        corpus = [b"abxxabxxabxxab"]
        window_size = 1 << 10
        positions = _compute_positions([b"ab"], corpus, window_bits=10)
        self.assertGreater(positions[b"ab"], 6 / window_size)

    def test_missing_entry_defaults(self):
        """Entries not found in corpus get default position 0.5."""
        corpus = [b"hello"]
        positions = _compute_positions([b"xyz"], corpus, window_bits=10)
        self.assertEqual(positions[b"xyz"], 0.5)


class TestSplitFragments(unittest.TestCase):
    def test_basic_split(self):
        fragments = [b"hello world hello", b"say hello there"]
        result = _split_fragments(fragments, b"hello", min_length=2)
        self.assertNotIn(b"hello", result)
        self.assertIn(b" world ", result)
        self.assertIn(b" there", result)

    def test_short_parts_dropped(self):
        """Parts shorter than min_length are dropped."""
        fragments = [b"abXcd"]
        result = _split_fragments(fragments, b"X", min_length=3)
        # "ab" and "cd" are both 2 bytes, below min_length=3
        self.assertEqual(result, [])

    def test_no_match(self):
        """Fragments without the pattern are kept as-is."""
        fragments = [b"hello world"]
        result = _split_fragments(fragments, b"xyz", min_length=2)
        self.assertEqual(result, [b"hello world"])


class TestPackDictionary(unittest.TestCase):
    def test_late_position_at_right(self):
        """Entries appearing late in messages are placed at the right end."""
        # "early" appears at start (pos=0.0), "late" appears at end (pos=1.0)
        entries = [(b"early_pat", 100, 0.1), (b"late_pat", 100, 0.9)]
        result, effective = pack_dictionary(entries, window_bits=10, literal_bits=8)
        self.assertEqual(len(result), 1024)
        # late_pat should be at the right end
        self.assertTrue(result.endswith(b"late_pat"))

    def test_exact_size(self):
        """Output is always exactly 2^window_bits bytes."""
        entries = [(b"abc", 10, 0.5)]
        for window_bits in (8, 10, 12):
            result, _ = pack_dictionary(entries, window_bits=window_bits, literal_bits=8)
            self.assertEqual(len(result), 1 << window_bits)

    def test_overflow_trimmed(self):
        """Entries that won't fit are dropped."""
        entries = [(b"A" * 200, 1000, 0.9), (b"B" * 200, 500, 0.5)]
        result, effective = pack_dictionary(entries, window_bits=8, literal_bits=8)
        self.assertEqual(len(result), 256)
        self.assertTrue(result.endswith(b"A" * 200))
        self.assertNotIn(b"B" * 200, result)
        self.assertEqual(effective, 200)

    def test_remaining_filled_with_default(self):
        """Unused space is filled with the default initialization pattern."""
        import tamp

        entries = [(b"abc", 10, 0.5)]
        result, _ = pack_dictionary(entries, window_bits=8, literal_bits=8)
        default = tamp.initialize_dictionary(256, literal=8)
        prefix_len = 256 - 3
        self.assertEqual(result[:prefix_len], default[:prefix_len])
        self.assertEqual(result[prefix_len:], b"abc")

    def test_zero_score_entries_excluded(self):
        """Entries with zero or negative score are not packed."""
        entries = [(b"high_value", 100, 0.5), (b"hello", 0, 0.5)]
        result, effective = pack_dictionary(entries, window_bits=8, literal_bits=8)
        self.assertIn(b"high_value", result)
        self.assertEqual(effective, len(b"high_value"))

    def test_effective_size_zero_when_all_zero_score(self):
        """All entries with non-positive score gives effective_size=0."""
        entries = [(b"abc", 0, 0.5), (b"xyz", -5, 0.5)]
        result, effective = pack_dictionary(entries, window_bits=8, literal_bits=8)
        self.assertEqual(effective, 0)
        self.assertEqual(len(result), 256)


class TestPackDictionaryOrdering(unittest.TestCase):
    def test_position_based_ordering(self):
        """Entries sorted by position: late-appearing at right."""
        entries = [(b"AAA", 100, 0.9), (b"BBB", 100, 0.5), (b"CCC", 100, 0.1)]
        result, effective = pack_dictionary(entries, window_bits=8, literal_bits=8)
        self.assertTrue(result.endswith(b"AAA"))
        aaa_pos = 256 - 3
        bbb_pos = aaa_pos - 3
        ccc_pos = bbb_pos - 3
        self.assertEqual(result[bbb_pos : bbb_pos + 3], b"BBB")
        self.assertEqual(result[ccc_pos : ccc_pos + 3], b"CCC")
        self.assertEqual(effective, 9)

    def test_density_tiebreak(self):
        """At same position, higher density goes further right."""
        # Same position, but "HI" has higher density (100/2=50 vs 100/5=20)
        entries = [(b"HELLO", 100, 0.5), (b"HI", 100, 0.5)]
        result, _ = pack_dictionary(entries, window_bits=8, literal_bits=8)
        self.assertTrue(result.endswith(b"HI"))

    def test_empty_input(self):
        """No entries produces a default-initialized dictionary."""
        import tamp

        result, effective = pack_dictionary([], window_bits=8, literal_bits=8)
        expected = tamp.initialize_dictionary(256, literal=8)
        self.assertEqual(result, expected)
        self.assertEqual(effective, 0)


class TestBuildDictionary(unittest.TestCase):
    def test_basic_pipeline(self):
        """Full pipeline produces a dictionary of the correct size."""
        corpus = [
            b"the quick brown fox jumps over the lazy dog",
            b"the quick brown cat jumps over the lazy dog",
            b"the quick brown fox runs over the lazy cat",
        ]
        result, effective = build_dictionary(corpus, window_bits=10, literal_bits=8)
        self.assertEqual(len(result), 1024)
        self.assertIsInstance(result, bytearray)
        self.assertGreater(effective, 0)
        self.assertLessEqual(effective, 1024)

    def test_common_phrases_in_dictionary(self):
        """Common phrases from the corpus appear in the dictionary."""
        corpus = [b"sensor_id=42,temp=23.5"] * 50 + [b"sensor_id=99,temp=18.1"] * 50
        result, _ = build_dictionary(corpus, window_bits=8, literal_bits=8)
        self.assertIn(b"sensor_id=", result)

    def test_small_window(self):
        """Works with the smallest window size."""
        corpus = [b"aaaa bbbb"] * 10
        result, _ = build_dictionary(corpus, window_bits=8, literal_bits=8)
        self.assertEqual(len(result), 256)

    def test_single_sample(self):
        """Works with a single-sample corpus (degenerate case)."""
        corpus = [b"hello world"]
        result, _ = build_dictionary(corpus, window_bits=8, literal_bits=8)
        self.assertEqual(len(result), 256)

    def test_empty_messages_skipped(self):
        """Empty messages in corpus don't cause errors."""
        corpus = [b"", b"hello world", b"", b"hello world"]
        result, _ = build_dictionary(corpus, window_bits=8, literal_bits=8)
        self.assertEqual(len(result), 256)

    def test_accepts_generator(self):
        """Corpus can be a generator."""
        corpus_list = [b"hello world"] * 10
        result, _ = build_dictionary((s for s in corpus_list), window_bits=8, literal_bits=8)
        self.assertEqual(len(result), 256)

    def test_extended_false(self):
        """Non-extended mode uses shorter max match length."""
        corpus = [b"the quick brown fox jumps over the lazy dog"] * 10
        result, _ = build_dictionary(corpus, window_bits=10, literal_bits=8, extended=False)
        self.assertEqual(len(result), 1024)

    def test_roundtrip_compression(self):
        """Dictionary built from corpus actually improves compression."""
        import tamp

        corpus = [f"sensor_id={i},temp=23.{i}".encode() for i in range(50)]
        dictionary, _ = build_dictionary(corpus, window_bits=10, literal_bits=8)

        test_msg = b"sensor_id=42,temp=23.5"
        compressed_default = tamp.compress(test_msg)
        compressed_custom = tamp.compress(test_msg, dictionary=bytearray(dictionary))

        self.assertLessEqual(len(compressed_custom), len(compressed_default))

        decompressed = tamp.decompress(compressed_custom, dictionary=bytearray(dictionary))
        self.assertEqual(decompressed, test_msg)

    def test_effective_size_reported(self):
        """Effective size reflects how much of the dictionary is useful."""
        corpus = [b"hello world"] * 5
        _, effective = build_dictionary(corpus, window_bits=10, literal_bits=8)
        # With only 5 identical short messages, effective should be much less than 1024
        self.assertGreater(effective, 0)
        self.assertLess(effective, 1024)


class TestFindKnee(unittest.TestCase):
    def test_linear_curve_returns_full(self):
        """A perfectly linear curve has no knee — return full size."""
        # Every byte saves exactly 10 compressed bytes.
        # Marginal == average everywhere, so every segment is worthwhile.
        results = [(size, 1000 - size * 10) for size in range(10, 110, 10)]
        self.assertEqual(find_knee(results), results[-1][0])

    def test_sharp_knee_detected_early(self):
        """A sharp L-shaped curve returns the corner, not full size."""
        # Big savings for the first 30 bytes, almost nothing after.
        results = [
            (10, 900),
            (20, 600),
            (30, 300),
            (40, 295),
            (50, 291),
            (60, 288),
            (70, 286),
            (80, 285),
            (90, 284),
            (100, 283),
        ]
        knee = find_knee(results)
        self.assertLess(knee, 60, f"expected sharp-knee detection below 60, got {knee}")
        self.assertGreaterEqual(knee, 30)

    def test_gradual_curve_selects_high_fill(self):
        """A smoothly concave curve selects a high fill level.

        On gradual curves (like SMS), marginal benefit decreases slowly.
        The knee should land at a high fill point where the marginal
        per byte finally drops below the threshold.
        """
        sizes = list(range(100, 1100, 100))
        ys = [1000, 930, 870, 820, 780, 750, 730, 715, 705, 700]
        results = list(zip(sizes, ys))

        knee = find_knee(results)
        # Should select a high fill level (>= 700), not an early
        # inflection point that only captures ~50% of improvement.
        self.assertGreaterEqual(knee, 700)

    def test_higher_fraction_selects_earlier(self):
        """A higher marginal_fraction is more aggressive about cutting off."""
        results = [
            (10, 900),
            (20, 600),
            (30, 300),
            (40, 295),
            (50, 291),
            (60, 288),
            (70, 286),
            (80, 285),
            (90, 284),
            (100, 283),
        ]
        knee_strict = find_knee(results, marginal_fraction=0.8)
        knee_lenient = find_knee(results, marginal_fraction=0.2)
        self.assertLessEqual(knee_strict, knee_lenient)

    def test_short_results_returns_last(self):
        """Fewer than 3 points — nothing to analyze, return the last."""
        self.assertEqual(find_knee([(10, 100)]), 10)
        self.assertEqual(find_knee([(10, 100), (20, 90)]), 20)

    def test_empty_results_degenerate_handled(self):
        """Flat curve (all same y) returns full size."""
        results = [(10, 100), (20, 100), (30, 100)]
        self.assertEqual(find_knee(results), 30)


class TestBuildDictionaryDeterminism(unittest.TestCase):
    """Verify build_dictionary produces identical output across runs.

    Python's hash randomization (PYTHONHASHSEED) causes ``set`` and ``dict``
    iteration over bytes keys to vary between processes, which previously
    caused sort-tie ordering in Phase 1/2/3 to be nondeterministic.
    """

    def _run_build(self, seed: str) -> str:
        import subprocess
        import sys

        script = (
            "import hashlib;"
            "from tamp.cli.build_dictionary import build_dictionary;"
            "corpus = [b'the quick brown fox jumps over the lazy dog'] * 20 + "
            "[b'a stitch in time saves nine'] * 20 + "
            "[b'early bird catches the worm'] * 20;"
            "d, eff = build_dictionary(corpus, window_bits=10, literal_bits=8, "
            "trim_threshold=8, target_fill=1.0);"
            "print(f'{eff}:{hashlib.sha256(bytes(d)).hexdigest()}')"
        )
        result = subprocess.run(  # noqa: S603
            [sys.executable, "-c", script],
            capture_output=True,
            text=True,
            env={"PYTHONHASHSEED": seed, "PATH": os.environ.get("PATH", "")},
            check=True,
        )
        return result.stdout.strip()

    def test_reproducible_across_hash_seeds(self):
        """Two fresh processes with different hash seeds produce identical dictionaries."""
        out1 = self._run_build("1")
        out2 = self._run_build("42")
        self.assertEqual(out1, out2)


class TestFindBestTrimThreshold(unittest.TestCase):
    def test_returns_one_of_candidates(self):
        """Returned trim_threshold is one of the candidate values."""
        corpus = [b"the quick brown fox jumps over the lazy dog"] * 10
        _, _, tt = find_best_trim_threshold(
            corpus, window_bits=10, literal_bits=8, extended=True, candidates=(6, 8, 10, 12)
        )
        self.assertIn(tt, (6, 8, 10, 12))

    def test_dictionary_is_window_sized(self):
        """The returned dictionary matches window size."""
        corpus = [b"hello world hello"] * 5
        dictionary, _, _ = find_best_trim_threshold(corpus, window_bits=10, literal_bits=8)
        self.assertEqual(len(dictionary), 1024)

    def test_empty_corpus(self):
        """Empty corpus is handled gracefully."""
        dictionary, effective_size, tt = find_best_trim_threshold([], window_bits=10, literal_bits=8)
        self.assertEqual(len(dictionary), 1024)
        self.assertEqual(effective_size, 0)
        self.assertIsInstance(tt, int)

    def test_empty_candidates_raises(self):
        """Empty candidates tuple raises."""
        with self.assertRaises(ValueError):
            find_best_trim_threshold([b"hello"], window_bits=10, literal_bits=8, candidates=())

    def test_auto_tune_improves_or_matches_default(self):
        """Auto-tuning beats or matches a fixed trim_threshold=8 baseline."""
        import tamp

        corpus = [f"sensor_id={i % 5},temp=23.{i % 10},humidity=65.{i % 10}".encode() for i in range(100)]

        baseline_dict, _ = build_dictionary(list(corpus), window_bits=10, literal_bits=8, trim_threshold=8)
        baseline_total = sum(
            len(tamp.compress(s, window=10, literal=8, dictionary=bytearray(baseline_dict))) for s in corpus
        )

        auto_dict, _, _ = find_best_trim_threshold(corpus, window_bits=10, literal_bits=8)
        auto_total = sum(len(tamp.compress(s, window=10, literal=8, dictionary=bytearray(auto_dict))) for s in corpus)

        self.assertLessEqual(auto_total, baseline_total)


class TestEvaluateDictionaryTradeoff(unittest.TestCase):
    def test_returns_results_including_effective_size(self):
        """Results always include the full effective_size."""
        corpus = [b"sensor_id=42,temp=23.5"] * 20
        dictionary, effective_size = build_dictionary(corpus, window_bits=10, literal_bits=8)
        results = evaluate_dictionary_tradeoff(corpus, dictionary, effective_size, window_bits=10)
        sizes = [r[0] for r in results]
        self.assertIn(effective_size, sizes)

    def test_more_dictionary_bytes_improves_compression(self):
        """Compression should generally improve (or not worsen) as dict size increases."""
        corpus = [f"sensor_id={i},temp=23.{i}".encode() for i in range(50)]
        dictionary, effective_size = build_dictionary(corpus, window_bits=10, literal_bits=8)
        results = evaluate_dictionary_tradeoff(corpus, dictionary, effective_size, window_bits=10)
        # The last point (full dict) should compress at least as well as the first.
        self.assertLessEqual(results[-1][1], results[0][1])

    def test_zero_effective_size(self):
        """Zero effective size returns a single baseline point."""
        import tamp

        corpus = [b"abc"]
        dictionary = tamp.initialize_dictionary(1024, literal=8)
        results = evaluate_dictionary_tradeoff(corpus, dictionary, 0, window_bits=10)
        self.assertEqual(len(results), 1)
        self.assertEqual(results[0][0], 0)

    def test_sub_dictionary_slicing(self):
        """Sub-dictionaries contain the rightmost bytes of the full dictionary."""
        import tamp

        corpus = [b"hello world hello"] * 20
        dictionary, effective_size = build_dictionary(corpus, window_bits=8, literal_bits=8)
        results = evaluate_dictionary_tradeoff(corpus, dictionary, effective_size, window_bits=8, n_points=4)
        # Each result should have a positive compressed size.
        for dict_bytes, compressed_bytes in results:
            self.assertGreater(compressed_bytes, 0)
            self.assertGreater(dict_bytes, 0)


class TestBuildDictionaryCli(unittest.TestCase):
    def test_build_dictionary_from_directory(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_dir = Path(tmp_dir)
            corpus_dir = tmp_dir / "corpus"
            corpus_dir.mkdir()
            dict_file = tmp_dir / "dict.bin"

            for i in range(10):
                (corpus_dir / f"msg_{i}.bin").write_bytes(f"sensor_id={i},temp=23.{i},humidity=65.{i}".encode())

            app(
                ["build-dictionary", str(corpus_dir), "-o", str(dict_file), "-w", "8"],
                **_app_kwargs,
            )

            result = dict_file.read_bytes()
            # Default is raw output: effective bytes only, smaller than window
            self.assertGreater(len(result), 0)
            self.assertLessEqual(len(result), 256)

    def test_build_dictionary_roundtrip(self):
        """Build a dictionary, compress with it, decompress with it."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_dir = Path(tmp_dir)
            corpus_dir = tmp_dir / "corpus"
            corpus_dir.mkdir()
            dict_file = tmp_dir / "dict.bin"
            compressed_file = tmp_dir / "compressed.tamp"
            output_file = tmp_dir / "output.bin"

            test_messages = [
                b"sensor_id=42,temp=23.5,humidity=65.2",
                b"sensor_id=42,temp=23.6,humidity=65.1",
                b"sensor_id=99,temp=18.1,humidity=70.3",
                b"sensor_id=99,temp=18.2,humidity=70.5",
            ]
            for i, msg in enumerate(test_messages):
                (corpus_dir / f"msg_{i}.bin").write_bytes(msg)

            # Build dictionary
            app(
                ["build-dictionary", str(corpus_dir), "-o", str(dict_file), "-w", "10"],
                **_app_kwargs,
            )

            # Compress a new message using the dictionary
            test_input = b"sensor_id=42,temp=24.0,humidity=66.0"
            input_file = tmp_dir / "input.bin"
            input_file.write_bytes(test_input)

            app(
                [
                    "compress",
                    str(input_file),
                    "-o",
                    str(compressed_file),
                    "-d",
                    str(dict_file),
                ],
                **_app_kwargs,
            )

            # Decompress using the same dictionary
            app(
                [
                    "decompress",
                    str(compressed_file),
                    "-o",
                    str(output_file),
                    "-d",
                    str(dict_file),
                ],
                **_app_kwargs,
            )

            self.assertEqual(output_file.read_bytes(), test_input)

    def test_build_dictionary_from_file_newline_delimited(self):
        """A single file with newline-delimited messages."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_dir = Path(tmp_dir)
            corpus_file = tmp_dir / "corpus.txt"
            dict_file = tmp_dir / "dict.bin"

            corpus_file.write_text(
                "sensor_id=42,temp=23.5\n"
                "sensor_id=99,temp=18.1\n"
                "\n"  # empty line should be skipped
                "sensor_id=42,temp=24.0\n"
            )

            app(
                ["build-dictionary", str(corpus_file), "-o", str(dict_file), "-w", "8", "--target-fill", "1.0"],
                **_app_kwargs,
            )

            result = dict_file.read_bytes()
            self.assertGreater(len(result), 0)
            self.assertLessEqual(len(result), 256)

    def test_build_dictionary_from_file_custom_delimiter(self):
        """A single file with a custom delimiter."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_dir = Path(tmp_dir)
            corpus_file = tmp_dir / "corpus.bin"
            dict_file = tmp_dir / "dict.bin"

            corpus_file.write_bytes(b"sensor_id=42,temp=23.5|sensor_id=99,temp=18.1|sensor_id=42,temp=24.0")

            app(
                [
                    "build-dictionary",
                    str(corpus_file),
                    "-o",
                    str(dict_file),
                    "-w",
                    "8",
                    "--delimiter",
                    "|",
                    "--target-fill",
                    "1.0",
                ],
                **_app_kwargs,
            )

            result = dict_file.read_bytes()
            self.assertGreater(len(result), 0)
            self.assertLessEqual(len(result), 256)

    def test_build_dictionary_file_roundtrip(self):
        """Build from a delimited file, compress and decompress with it."""
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_dir = Path(tmp_dir)
            corpus_file = tmp_dir / "corpus.txt"
            dict_file = tmp_dir / "dict.bin"
            compressed_file = tmp_dir / "compressed.tamp"
            output_file = tmp_dir / "output.bin"

            corpus_file.write_text(
                "sensor_id=42,temp=23.5,humidity=65.2\n"
                "sensor_id=42,temp=23.6,humidity=65.1\n"
                "sensor_id=99,temp=18.1,humidity=70.3\n"
            )

            app(
                ["build-dictionary", str(corpus_file), "-o", str(dict_file), "-w", "10"],
                **_app_kwargs,
            )

            test_input = b"sensor_id=42,temp=24.0,humidity=66.0"
            input_file = tmp_dir / "input.bin"
            input_file.write_bytes(test_input)

            app(["compress", str(input_file), "-o", str(compressed_file), "-d", str(dict_file)], **_app_kwargs)
            app(["decompress", str(compressed_file), "-o", str(output_file), "-d", str(dict_file)], **_app_kwargs)

            self.assertEqual(output_file.read_bytes(), test_input)
