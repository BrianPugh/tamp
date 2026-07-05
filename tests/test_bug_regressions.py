"""Regression tests for specific fixed bugs in the Python bindings.

Each test names the bug it guards against, covering the pure-Python and
Cython implementations.
"""

import io
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


@unittest.skipIf(micropython is not None, "CPython-implementation regression tests")
class TestBugRegressions(unittest.TestCase):
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
