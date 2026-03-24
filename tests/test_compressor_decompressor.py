import random
import unittest
from io import BytesIO

try:
    import micropython
except ImportError:
    micropython = None

if micropython is None:
    # CPython: test pure Python and Cython implementations
    from tamp.compressor import Compressor as PyCompressor
    from tamp.decompressor import Decompressor as PyDecompressor

    try:
        from tamp._c_compressor import Compressor as CCompressor
        from tamp._c_decompressor import Decompressor as CDecompressor
    except ImportError:
        CCompressor = None
        CDecompressor = None

    NativeCompressor = None
    NativeDecompressor = None
else:
    # MicroPython: only test Native implementation
    # Pure Python and Cython implementations use CPython-specific features
    PyCompressor = None
    PyDecompressor = None
    CCompressor = None
    CDecompressor = None

    try:
        from tamp_native import Compressor as NativeCompressor
        from tamp_native import Decompressor as NativeDecompressor
    except ImportError:
        print("Skipping Native Module.")
        NativeCompressor = None
        NativeDecompressor = None


Compressors = (PyCompressor, CCompressor, NativeCompressor)
Decompressors = (PyDecompressor, CDecompressor, NativeDecompressor)


def walk_compressors_decompressors():
    """Iterate over all available compressor/decompressor combintations."""
    for Compressor in Compressors:
        if Compressor is None:
            continue

        for Decompressor in Decompressors:
            if Decompressor is None:
                continue

            yield (Compressor, Decompressor)


tale_of_two_cities = b"It was the best of times, it was the worst of times, it was the age of wisdom, it was the age of foolishness, it was the epoch of belief, it was the epoch of incredulity, it was the season of Light, it was the season of Darkness, it was the spring of hope, it was the winter of despair, we had everything before us, we had nothing before us, we were all going direct to Heaven, we were all going direct the other way - in short, the period was so far like the present period, that some of its noisiest authorities insisted on its being received, for good or for evil, in the superlative degree of comparison only."


class TestCompressorAndDecompressor(unittest.TestCase):
    def _autotest(self, num_bytes, n_bits, compressor_kwargs=None):
        if compressor_kwargs is None:
            compressor_kwargs = {}

        data = bytearray(random.randint(0, (1 << n_bits) - 1) for x in range(num_bytes))

        for Compressor, Decompressor in walk_compressors_decompressors():
            # Compress/Decompress random data
            # fmt: off
            with BytesIO() as f, \
                 self.subTest(data="Random", Compressor=Compressor, Decompressor=Decompressor):
                # fmt: on
                c = Compressor(f, **compressor_kwargs)
                c.write(data)
                c.flush()

                f.seek(0)
                d = Decompressor(f)
                actual = d.read()

                self.assertEqual(actual, data)

            # Compress/Decompress
            data = bytearray(1 for _ in range(num_bytes))
            # fmt: off
            with BytesIO() as f, \
                 self.subTest(data="Sequential", Compressor=Compressor, Decompressor=Decompressor):
                # fmt: on
                c = Compressor(f, **compressor_kwargs)
                c.write(data)
                c.flush()

                f.seek(0)
                d = Decompressor(f)
                actual = d.read()

                self.assertEqual(len(actual), len(data))
                self.assertEqual(actual, data)

    def test_default(self):
        self._autotest(10_000, 8)

    def test_7bit(self):
        self._autotest(10_000, 7, compressor_kwargs={"literal": 7})

    def test_6bit(self):
        self._autotest(10_000, 6, compressor_kwargs={"literal": 6})

    def test_5bit(self):
        self._autotest(10_000, 5, compressor_kwargs={"literal": 5})

    def test_tale_of_two_cities(self):
        assert len(tale_of_two_cities) > (1 << 8)
        for Compressor, Decompressor in walk_compressors_decompressors():
            with BytesIO() as f:
                c = Compressor(f, window=8)
                c.write(tale_of_two_cities)
                c.flush()

                f.seek(0)
                d = Decompressor(f)
                actual = d.read()

                assert actual == tale_of_two_cities

    def test_extended_explicit(self):
        """Explicit extended=True round-trip with random data."""
        self._autotest(10_000, 8, compressor_kwargs={"extended": True})

    def test_extended_rle_heavy(self):
        """Round-trip with input designed to trigger RLE encoding."""
        # Long runs of the same byte
        data = b"A" * 200 + b"B" * 100 + b"C" * 50 + b"D" * 30
        for Compressor, Decompressor in walk_compressors_decompressors():
            with BytesIO() as f, self.subTest(Compressor=Compressor, Decompressor=Decompressor):
                c = Compressor(f, extended=True)
                c.write(data)
                c.flush()

                f.seek(0)
                d = Decompressor(f)
                actual = d.read()

                self.assertEqual(actual, data)

    def test_extended_match_heavy(self):
        """Round-trip with input designed to trigger extended match tokens."""
        # Repeating 16-byte pattern should trigger extended matches (>13 bytes)
        pattern = b"Hello, World!!! "  # 16 bytes
        data = pattern * 20  # 320 bytes
        for Compressor, Decompressor in walk_compressors_decompressors():
            with BytesIO() as f, self.subTest(Compressor=Compressor, Decompressor=Decompressor):
                c = Compressor(f, extended=True)
                c.write(data)
                c.flush()

                f.seek(0)
                d = Decompressor(f)
                actual = d.read()

                self.assertEqual(actual, data)

    def test_extended_lazy_matching(self):
        """Round-trip with both extended=True and lazy_matching=True."""
        for Compressor, Decompressor in walk_compressors_decompressors():
            if Compressor is NativeCompressor:
                continue  # Native module doesn't support lazy_matching kwarg

            # Random data
            data = bytearray(random.randint(0, 255) for _ in range(10_000))
            # fmt: off
            with BytesIO() as f, \
                 self.subTest(data="Random", Compressor=Compressor, Decompressor=Decompressor):
                # fmt: on
                c = Compressor(f, extended=True, lazy_matching=True)
                c.write(data)
                c.flush()

                f.seek(0)
                d = Decompressor(f)
                actual = d.read()

                self.assertEqual(actual, data)

            # Repetitive data (triggers both RLE and extended match)
            data = (b"The quick brown fox " * 10 + b"X" * 50) * 5
            # fmt: off
            with BytesIO() as f, \
                 self.subTest(data="Repetitive", Compressor=Compressor, Decompressor=Decompressor):
                # fmt: on
                c = Compressor(f, extended=True, lazy_matching=True)
                c.write(data)
                c.flush()

                f.seek(0)
                d = Decompressor(f)
                actual = d.read()

                self.assertEqual(actual, data)

    def test_extended_various_windows(self):
        """Round-trip with extended=True across various window sizes."""
        data = tale_of_two_cities
        for window in (8, 9, 10):
            for Compressor, Decompressor in walk_compressors_decompressors():
                # fmt: off
                with BytesIO() as f, \
                     self.subTest(window=window, Compressor=Compressor, Decompressor=Decompressor):
                    # fmt: on
                    c = Compressor(f, window=window, extended=True)
                    c.write(data)
                    c.flush()

                    f.seek(0)
                    d = Decompressor(f)
                    actual = d.read()

                    self.assertEqual(actual, data)

    def test_extended_rle_compresses_better(self):
        """Verify extended mode produces smaller output than non-extended for RLE data."""
        data = b"A" * 200
        for Compressor, Decompressor in walk_compressors_decompressors():
            with self.subTest(Compressor=Compressor, Decompressor=Decompressor):
                # Compress with extended=True
                with BytesIO() as f:
                    c = Compressor(f, extended=True)
                    c.write(data)
                    c.flush()
                    extended_size = f.tell()

                # Compress with extended=False
                with BytesIO() as f:
                    c = Compressor(f, extended=False)
                    c.write(data)
                    c.flush()
                    non_extended_size = f.tell()

                self.assertTrue(extended_size < non_extended_size)

    def test_extended_match_compresses_better(self):
        """Verify extended mode produces smaller output than non-extended for long match data."""
        pattern = b"Hello, World!!! "  # 16 bytes
        data = pattern * 20  # 320 bytes
        for Compressor, Decompressor in walk_compressors_decompressors():
            with self.subTest(Compressor=Compressor, Decompressor=Decompressor):
                # Compress with extended=True
                with BytesIO() as f:
                    c = Compressor(f, extended=True)
                    c.write(data)
                    c.flush()
                    extended_size = f.tell()

                # Compress with extended=False
                with BytesIO() as f:
                    c = Compressor(f, extended=False)
                    c.write(data)
                    c.flush()
                    non_extended_size = f.tell()

                self.assertTrue(extended_size < non_extended_size)

    def test_extended_rle_transition(self):
        """Round-trip with data that transitions between RLE runs and non-RLE content."""
        data = b"A" * 50 + b"The quick brown fox jumps!" + b"B" * 45
        for Compressor, Decompressor in walk_compressors_decompressors():
            with BytesIO() as f, self.subTest(Compressor=Compressor, Decompressor=Decompressor):
                c = Compressor(f, extended=True)
                c.write(data)
                c.flush()

                f.seek(0)
                d = Decompressor(f)
                actual = d.read()

                self.assertEqual(actual, data)

    def test_extended_rle_boundary(self):
        """Round-trip with RLE counts at boundary values."""
        for Compressor, Decompressor in walk_compressors_decompressors():
            # Minimum RLE (2 repeats)
            data = b"Z" * 2
            with BytesIO() as f, self.subTest(count=2, Compressor=Compressor, Decompressor=Decompressor):
                c = Compressor(f, extended=True)
                c.write(data)
                c.flush()
                f.seek(0)
                d = Decompressor(f)
                self.assertEqual(d.read(), data)

            # Maximum RLE (241 repeats)
            data = b"Z" * 241
            with BytesIO() as f, self.subTest(count=241, Compressor=Compressor, Decompressor=Decompressor):
                c = Compressor(f, extended=True)
                c.write(data)
                c.flush()
                f.seek(0)
                d = Decompressor(f)
                self.assertEqual(d.read(), data)

            # Just over max RLE (requires multiple tokens)
            data = b"Z" * 500
            with BytesIO() as f, self.subTest(count=500, Compressor=Compressor, Decompressor=Decompressor):
                c = Compressor(f, extended=True)
                c.write(data)
                c.flush()
                f.seek(0)
                d = Decompressor(f)
                self.assertEqual(d.read(), data)

    def test_reset_dictionary_basic(self):
        """Compress data, flush, reset dictionary, compress more, decompress all."""
        for Compressor, Decompressor in walk_compressors_decompressors():
            with BytesIO() as f, self.subTest(Compressor=Compressor, Decompressor=Decompressor):
                c = Compressor(f, dictionary_reset=True)
                c.write(tale_of_two_cities[:200])
                c.flush(write_token=True)
                c.reset_dictionary()
                c.write(tale_of_two_cities[200:])
                c.flush(write_token=False)

                f.seek(0)
                d = Decompressor(f)
                actual = d.read()

                self.assertEqual(actual, tale_of_two_cities)

    def test_reset_dictionary_no_prior_flush(self):
        """Reset dictionary directly without explicit flush first."""
        data1 = b"Hello world! " * 20
        data2 = b"Goodbye world! " * 20
        for Compressor, Decompressor in walk_compressors_decompressors():
            with BytesIO() as f, self.subTest(Compressor=Compressor, Decompressor=Decompressor):
                c = Compressor(f, dictionary_reset=True)
                c.write(data1)
                c.reset_dictionary()
                c.write(data2)
                c.flush(write_token=False)

                f.seek(0)
                d = Decompressor(f)
                actual = d.read()

                self.assertEqual(actual, data1 + data2)

    def test_reset_dictionary_multiple_resets(self):
        """Multiple resets in a single stream."""
        data1 = b"Alpha " * 30
        data2 = b"Beta " * 30
        data3 = b"Gamma " * 30
        for Compressor, Decompressor in walk_compressors_decompressors():
            with BytesIO() as f, self.subTest(Compressor=Compressor, Decompressor=Decompressor):
                c = Compressor(f, dictionary_reset=True)
                c.write(data1)
                c.reset_dictionary()
                c.write(data2)
                c.reset_dictionary()
                c.write(data3)
                c.flush(write_token=False)

                f.seek(0)
                d = Decompressor(f)
                actual = d.read()

                self.assertEqual(actual, data1 + data2 + data3)

    def test_reset_dictionary_immediate(self):
        """Reset immediately after init (no data compressed yet)."""
        data = tale_of_two_cities
        for Compressor, Decompressor in walk_compressors_decompressors():
            with BytesIO() as f, self.subTest(Compressor=Compressor, Decompressor=Decompressor):
                c = Compressor(f, dictionary_reset=True)
                c.reset_dictionary()
                c.write(data)
                c.flush(write_token=False)

                f.seek(0)
                d = Decompressor(f)
                actual = d.read()

                self.assertEqual(actual, data)

    def test_reset_dictionary_extended(self):
        """Reset with extended=True."""
        for Compressor, Decompressor in walk_compressors_decompressors():
            with BytesIO() as f, self.subTest(Compressor=Compressor, Decompressor=Decompressor):
                c = Compressor(f, extended=True, dictionary_reset=True)
                c.write(tale_of_two_cities[:200])
                c.flush(write_token=True)
                c.reset_dictionary()
                c.write(tale_of_two_cities[200:])
                c.flush(write_token=False)

                f.seek(0)
                d = Decompressor(f)
                actual = d.read()

                self.assertEqual(actual, tale_of_two_cities)

    def test_reset_dictionary_lazy_matching(self):
        """Reset with extended=True and lazy_matching=True."""
        for Compressor, Decompressor in walk_compressors_decompressors():
            if Compressor is NativeCompressor:
                continue
            with BytesIO() as f, self.subTest(Compressor=Compressor, Decompressor=Decompressor):
                c = Compressor(f, extended=True, lazy_matching=True, dictionary_reset=True)
                c.write(tale_of_two_cities[:200])
                c.flush(write_token=True)
                c.reset_dictionary()
                c.write(tale_of_two_cities[200:])
                c.flush(write_token=False)

                f.seek(0)
                d = Decompressor(f)
                actual = d.read()

                self.assertEqual(actual, tale_of_two_cities)

    def test_reset_dictionary_small_window(self):
        """Reset with smallest window size (window=8)."""
        data1 = tale_of_two_cities[:100]
        data2 = tale_of_two_cities[100:200]
        for Compressor, Decompressor in walk_compressors_decompressors():
            with BytesIO() as f, self.subTest(Compressor=Compressor, Decompressor=Decompressor):
                c = Compressor(f, window=8, dictionary_reset=True)
                c.write(data1)
                c.reset_dictionary()
                c.write(data2)
                c.flush(write_token=False)

                f.seek(0)
                d = Decompressor(f)
                actual = d.read()

                self.assertEqual(actual, data1 + data2)

    def test_reset_dictionary_different_literals(self):
        """Reset with different literal values (5, 6, 7)."""
        for literal in (5, 6, 7):
            n_bits = literal
            data1 = bytearray(random.randint(0, (1 << n_bits) - 1) for _ in range(200))
            data2 = bytearray(random.randint(0, (1 << n_bits) - 1) for _ in range(200))
            for Compressor, Decompressor in walk_compressors_decompressors():
                with BytesIO() as f, self.subTest(literal=literal, Compressor=Compressor, Decompressor=Decompressor):
                    c = Compressor(f, literal=literal, dictionary_reset=True)
                    c.write(data1)
                    c.reset_dictionary()
                    c.write(data2)
                    c.flush(write_token=False)

                    f.seek(0)
                    d = Decompressor(f)
                    actual = d.read()

                    self.assertEqual(actual, data1 + data2)

    def test_reset_dictionary_rle_boundary(self):
        """Reset after compressing RLE-heavy data."""
        data1 = b"A" * 200
        data2 = b"The quick brown fox jumps over the lazy dog. " * 5
        for Compressor, Decompressor in walk_compressors_decompressors():
            with BytesIO() as f, self.subTest(Compressor=Compressor, Decompressor=Decompressor):
                c = Compressor(f, extended=True, dictionary_reset=True)
                c.write(data1)
                c.reset_dictionary()
                c.write(data2)
                c.flush(write_token=False)

                f.seek(0)
                d = Decompressor(f)
                actual = d.read()

                self.assertEqual(actual, data1 + data2)

    def test_reset_dictionary_cross_impl(self):
        """Cross-implementation: compress with Py, decompress with C (and vice versa)."""
        if PyCompressor is None or CCompressor is None or PyDecompressor is None or CDecompressor is None:
            self.skipTest("Need both Py and C implementations")

        data1 = tale_of_two_cities[:200]
        data2 = tale_of_two_cities[200:]

        # Py compress, C decompress
        with BytesIO() as f, self.subTest(compress="Py", decompress="C"):
            c = PyCompressor(f, dictionary_reset=True)
            c.write(data1)
            c.reset_dictionary()
            c.write(data2)
            c.flush(write_token=False)

            f.seek(0)
            d = CDecompressor(f)
            actual = d.read()
            self.assertEqual(actual, tale_of_two_cities)

        # C compress, Py decompress
        with BytesIO() as f, self.subTest(compress="C", decompress="Py"):
            c = CCompressor(f, dictionary_reset=True)
            c.write(data1)
            c.reset_dictionary()
            c.write(data2)
            c.flush(write_token=False)

            f.seek(0)
            d = PyDecompressor(f)
            actual = d.read()
            self.assertEqual(actual, tale_of_two_cities)


if __name__ == "__main__":
    unittest.main()
