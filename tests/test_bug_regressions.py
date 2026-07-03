"""Regression tests for specific fixed bugs.

Each test names the bug it guards against. The C-core equivalents (low-level
sink/poll byte loss, stale lazy-match cache) are additionally covered by
ctests/test_compressor.c, which exercises the C library directly.
"""

import io
import random
import unittest

try:
    import micropython
except ImportError:
    micropython = None

if micropython is None:
    from tamp import initialize_dictionary
    from tamp.compressor import Compressor as PyCompressor
    from tamp.decompressor import Decompressor as PyDecompressor

    try:
        from tamp._c_compressor import Compressor as CCompressor
        from tamp._c_decompressor import Decompressor as CDecompressor
    except ImportError:
        CCompressor = None
        CDecompressor = None

    PAIRS = [
        (name, c, d)
        for name, c, d in (
            ("python", PyCompressor, PyDecompressor),
            ("cython", CCompressor, CDecompressor),
        )
        if c is not None
    ]
else:
    PAIRS = []


def _run_heavy_data(rng, size=512):
    """Two-letter alphabet with short runs; reliably exercised the stale
    lazy-match cache on pre-fix code (~5% of seeds corrupted).
    """
    out = bytearray()
    while len(out) < size:
        if rng.getrandbits(1):
            out += bytes([rng.choice((ord("a"), ord("b")))]) * (1 + rng.getrandbits(2) % 4)
        else:
            out += bytes(rng.choice((ord("a"), ord("b"))) for _ in range(1 + rng.getrandbits(1)))
    return bytes(out[:size])


@unittest.skipIf(micropython is not None, "CPython-implementation regression tests")
class TestBugRegressions(unittest.TestCase):
    def _roundtrip(self, compressor_cls, decompressor_cls, data, **compressor_kwargs):
        with io.BytesIO() as f:
            c = compressor_cls(f, **compressor_kwargs)
            c.write(data)
            c.flush(write_token=False)
            f.seek(0)
            return bytes(decompressor_cls(f).read())

    def test_lazy_extended_rle_roundtrip_fuzz(self):
        # Bug: with lazy_matching + extended, the RLE path consumed input
        # without invalidating the cached lazy match, silently corrupting output.
        for name, Compressor, Decompressor in PAIRS:
            with self.subTest(implementation=name):
                for seed in range(150):
                    rng = random.Random(seed)
                    data = _run_heavy_data(rng)
                    result = self._roundtrip(Compressor, Decompressor, data, lazy_matching=True)
                    self.assertEqual(result, data, f"corrupt roundtrip at seed {seed}")

    def test_rle_lone_byte_not_dropped(self):
        # Bug: a lone run byte accumulated by one compression cycle was dropped
        # when a later cycle saw the run had ended ("AAB" decompressed to "AB").
        # Drives the pure-Python internals cycle-by-cycle like C sink/poll.
        f = io.BytesIO()
        c = PyCompressor(f)
        payload = b"AAB"
        for b in payload:
            c.write(bytes([b]))
            while c._input_buffer:
                c._compress_input_buffer_single()
        c.flush(write_token=False)
        f.seek(0)
        self.assertEqual(bytes(PyDecompressor(f).read()), payload)

    def test_read_size_past_end_returns_short(self):
        # Bug: read(size) returned `size` bytes zero-padded past end-of-stream.
        for name, Compressor, Decompressor in PAIRS:
            with self.subTest(implementation=name), io.BytesIO() as f:
                c = Compressor(f)
                c.write(b"hello")
                c.flush(write_token=False)
                f.seek(0)
                self.assertEqual(bytes(Decompressor(f).read(100)), b"hello")

    def test_write_empty(self):
        # Bug: Cython Compressor.write(b"") raised IndexError.
        for name, Compressor, _ in PAIRS:
            with self.subTest(implementation=name), io.BytesIO() as f:
                self.assertEqual(Compressor(f).write(b""), 0)

    def test_decompressor_dictionary_size_mismatch_raises(self):
        # Bug: a wrong-size dictionary was used as the window buffer verbatim;
        # the Cython implementation let C write past the end of the bytearray.
        dictionary = initialize_dictionary(4096)
        with io.BytesIO() as f:
            c = PyCompressor(f, window=12, dictionary=bytearray(dictionary))
            c.write(b"payload " * 20)
            c.flush(write_token=False)
            compressed = f.getvalue()

        # 2560 is in (2**11, 2**12]: the old bit_size(len - 1) check accepted it
        # even though it is still too small for a window=12 stream.
        for wrong_size in (256, 2560, 0):
            for name, Compressor, Decompressor in PAIRS:
                with self.subTest(implementation=name, wrong_size=wrong_size):
                    with self.assertRaises(ValueError):
                        Decompressor(io.BytesIO(compressed), dictionary=bytearray(wrong_size))
                    with self.assertRaises(ValueError):
                        Compressor(io.BytesIO(), window=12, dictionary=bytearray(wrong_size))

        for name, _Compressor, Decompressor in PAIRS:
            with self.subTest(implementation=name):
                # Correct-size dictionary still round-trips.
                d = Decompressor(io.BytesIO(compressed), dictionary=bytearray(dictionary))
                self.assertEqual(bytes(d.read()), b"payload " * 20)

    def test_decompressor_unused_dictionary_reinitialized(self):
        # Bug: pure-Python used a supplied dictionary verbatim even when the
        # stream header's custom-dictionary bit was clear, corrupting output.
        payload = b"the quick brown fox jumps over the lazy dog" * 5
        for name, Compressor, Decompressor in PAIRS:
            with self.subTest(implementation=name), io.BytesIO() as f:
                c = Compressor(f)  # default dictionary
                c.write(payload)
                c.flush(write_token=False)
                f.seek(0)
                d = Decompressor(f, dictionary=bytearray(b"\xff" * 1024))
                self.assertEqual(bytes(d.read()), payload)

    def test_decompressor_read_only_stream(self):
        # Bug: Cython Decompressor accepted objects with only read() but later
        # required readinto(), raising AttributeError mid-stream.
        class ReadOnly:
            def __init__(self, data):
                self._f = io.BytesIO(data)

            def read(self, n=-1):
                return self._f.read(n)

        payload = b"read-only source works" * 20
        for name, Compressor, Decompressor in PAIRS:
            with self.subTest(implementation=name):
                with io.BytesIO() as f:
                    c = Compressor(f)
                    c.write(payload)
                    c.flush(write_token=False)
                    compressed = f.getvalue()
                self.assertEqual(bytes(Decompressor(ReadOnly(compressed)).read()), payload)


if __name__ == "__main__":
    unittest.main()
