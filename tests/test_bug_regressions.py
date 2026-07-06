"""Regression tests for specific fixed bugs in the Python bindings.

Each test names the bug it guards against, covering the pure-Python and
Cython implementations. ``TestOversizedDictionary`` also runs under
MicroPython against the pure-Python implementation.
"""

import io
import unittest

try:
    import micropython
except ImportError:
    micropython = None

from tamp import initialize_dictionary
from tamp.decompressor import Decompressor as PyDecompressor

if micropython is None:
    from tamp.compressor import Compressor as PyCompressor

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
    CCompressor = None
    CDecompressor = None
    PAIRS = []

# Decompressors supporting oversized dictionaries. The viper decompressor is
# deliberately stricter (exact size only) and is excluded.
DECOMPRESSORS = [("python", PyDecompressor)]
if CDecompressor is not None:
    DECOMPRESSORS.append(("cython", CDecompressor))


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

        # Oversized: compressors require an exact size (the caller chooses the
        # window, so it can always slice); decompressors accept it and use the
        # prefix (see TestOversizedDictionary).
        for name, Compressor, _Decompressor in PAIRS:
            with self.subTest(implementation=name), self.assertRaises(ValueError):
                Compressor(io.BytesIO(), window=12, dictionary=bytearray(8192))

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

    def test_decompressor_read_overreturning_stream_raises(self):
        # Bug: the read() fallback grew the internal input buffer when a
        # non-conforming file object returned more bytes than requested,
        # reallocating it out from under a cached pointer (silent corruption).
        if CDecompressor is None:
            self.skipTest("Cython implementation unavailable")

        class OverRead:
            def __init__(self, data):
                self._f = io.BytesIO(data)

            def read(self, n=-1):
                return self._f.read(n * 2 if n > 0 else -1)

        # Compressed stream must exceed CHUNK_SIZE (1 MiB) so that
        # read(CHUNK_SIZE) can over-return.
        import random

        payload = random.Random(0).randbytes(200_000) * 12
        with io.BytesIO() as f:
            c = CCompressor(f)
            c.write(payload)
            c.flush(write_token=False)
            compressed = f.getvalue()
        with self.assertRaises(ValueError):
            CDecompressor(OverRead(compressed)).read()

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


class TestOversizedDictionary(unittest.TestCase):
    """Decompressors accept dictionaries of at least 2**window bytes and use
    the first 2**window bytes in place; bytes past the window are never read
    or written. One long shared dictionary can thus serve compressors with
    different (unknown-in-advance) window sizes.

    Streams are hardcoded so these tests also run under MicroPython, where the
    pure-Python compressor is unavailable. Both were generated from
    ``PAYLOAD`` with window=10; regenerate with::

        f = io.BytesIO()
        c = Compressor(f, window=10, dictionary=bytearray(initialize_dictionary(4096)[:1024]))
        c.write(PAYLOAD); c.flush(write_token=False)   # -> CUSTOM_DICT_STREAM
        # DEFAULT_STREAM: same, with Compressor(f) (no dictionary)
    """

    PAYLOAD = b"payload " * 20
    CUSTOM_DICT_STREAM = bytes.fromhex("5eb8586f36c06cb248130009c8004f08004f320013c20000")
    DEFAULT_STREAM = bytes.fromhex("5ab8586f36c06cb248130009c8004f08004f320013c20000")

    def test_oversized_dictionary_uses_prefix_in_place(self):
        big = initialize_dictionary(4096)
        for name, Decompressor in DECOMPRESSORS:
            with self.subTest(implementation=name):
                oversized = bytearray(big)
                d = Decompressor(io.BytesIO(self.CUSTOM_DICT_STREAM), dictionary=oversized)
                self.assertEqual(bytes(d.read()), self.PAYLOAD)
                # The prefix is the live window, mutated in place; bytes past
                # the window are never written.
                self.assertNotEqual(oversized[:1024], big[:1024])
                self.assertEqual(oversized[1024:], big[1024:])

                # Bytes past the window must never influence output.
                garbage_tail = bytearray(big[:1024]) + bytearray(b"\xff" * 3072)
                d = Decompressor(io.BytesIO(self.CUSTOM_DICT_STREAM), dictionary=garbage_tail)
                self.assertEqual(bytes(d.read()), self.PAYLOAD)

                # A wrong prefix must not round-trip (guards against the size
                # check accidentally accepting a mismatched dictionary basis).
                wrong_prefix = bytearray(b"\x00" * 4096)
                d = Decompressor(io.BytesIO(self.CUSTOM_DICT_STREAM), dictionary=wrong_prefix)
                self.assertNotEqual(bytes(d.read()), self.PAYLOAD)

    def test_undersized_dictionary_raises(self):
        for wrong_size in (256, 1023, 0):
            for name, Decompressor in DECOMPRESSORS:
                with self.subTest(implementation=name, wrong_size=wrong_size), self.assertRaises(ValueError):
                    Decompressor(io.BytesIO(self.CUSTOM_DICT_STREAM), dictionary=bytearray(wrong_size))

    def test_unused_oversized_dictionary_reinitialized(self):
        # Oversized dictionary supplied for a stream that doesn't use a custom
        # dictionary: only the prefix is re-initialized (in place) and used;
        # output is correct and bytes past the window are never touched.
        for name, Decompressor in DECOMPRESSORS:
            with self.subTest(implementation=name):
                oversized = bytearray(b"\xff" * 4096)
                d = Decompressor(io.BytesIO(self.DEFAULT_STREAM), dictionary=oversized)
                self.assertEqual(bytes(d.read()), self.PAYLOAD)
                self.assertNotEqual(oversized[:1024], bytearray(b"\xff" * 1024))
                self.assertEqual(oversized[1024:], bytearray(b"\xff" * 3072))

    @unittest.skipIf(micropython is not None, "compressors unavailable on MicroPython")
    def test_live_compression_roundtrip(self):
        # Same scenario without fixtures: compress with each implementation
        # against the window-size prefix, decompress with the full dictionary.
        big = initialize_dictionary(4096)
        for cname, Compressor, _ in PAIRS:
            with io.BytesIO() as f:
                c = Compressor(f, window=10, dictionary=bytearray(big[:1024]))
                c.write(self.PAYLOAD)
                c.flush(write_token=False)
                compressed = f.getvalue()
            for dname, Decompressor in DECOMPRESSORS:
                with self.subTest(compressor=cname, decompressor=dname):
                    d = Decompressor(io.BytesIO(compressed), dictionary=bytearray(big))
                    self.assertEqual(bytes(d.read()), self.PAYLOAD)


if __name__ == "__main__":
    unittest.main()
