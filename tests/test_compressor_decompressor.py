import random
import unittest
from io import BytesIO

try:
    import micropython
except ImportError:
    micropython = None

from tamp.compressor import Compressor as PyCompressor
from tamp.decompressor import Decompressor as PyDecompressor

if micropython is None:
    ViperCompressor = None
    ViperDecompressor = None
    NativeCompressor = None
    NativeDecompressor = None
else:
    from tamp.compressor_viper import Compressor as ViperCompressor
    from tamp.decompressor_viper import Decompressor as ViperDecompressor

    try:
        from tamp_native import Compressor as NativeCompressor
        from tamp_native import Decompressor as NativeDecompressor
    except ImportError:
        print("Skipping Native Module.")
        NativeCompressor = None
        NativeDecompressor = None

try:
    from tamp._c_compressor import Compressor as CCompressor
    from tamp._c_decompressor import Decompressor as CDecompressor
except ImportError:
    CCompressor = None
    CDecompressor = None


Compressors = (PyCompressor, CCompressor, ViperCompressor, NativeCompressor)
Decompressors = (PyDecompressor, CDecompressor, ViperDecompressor, NativeDecompressor)


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
    def _autotest(self, num_bytes, n_bits, compressor_kwargs=None, v2_values=(False, True)):
        if compressor_kwargs is None:
            compressor_kwargs = {}

        data = bytearray(random.randint(0, (1 << n_bits) - 1) for x in range(num_bytes))

        for v2 in v2_values:
            kwargs = {**compressor_kwargs, "v2": v2}

            for Compressor, Decompressor in walk_compressors_decompressors():
                # Compress/Decompress random data
                with BytesIO() as f, self.subTest(
                    data="Random",
                    Compressor=Compressor,
                    Decompressor=Decompressor,
                    v2=v2,
                ):
                    c = Compressor(f, **kwargs)
                    c.write(data)
                    c.flush()

                    f.seek(0)
                    d = Decompressor(f)
                    actual = d.read()

                    self.assertEqual(actual, data)

                # Compress/Decompress sequential data
                data = bytearray(1 for _ in range(num_bytes))
                with BytesIO() as f, self.subTest(
                    data="Sequential",
                    Compressor=Compressor,
                    Decompressor=Decompressor,
                    v2=v2,
                ):
                    c = Compressor(f, **kwargs)
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
        for v2 in (False, True):
            for Compressor, Decompressor in walk_compressors_decompressors():
                with BytesIO() as f, self.subTest(
                    Compressor=Compressor,
                    Decompressor=Decompressor,
                    v2=v2,
                ):
                    c = Compressor(f, window=8, v2=v2)
                    c.write(tale_of_two_cities)
                    c.flush()

                    f.seek(0)
                    d = Decompressor(f)
                    actual = d.read()

                    assert actual == tale_of_two_cities

    def test_extended_match_python_c_identical(self):
        """Test that Python and C compressors produce identical output for extended matches."""
        if PyCompressor is None or CCompressor is None:
            self.skipTest("Python or C compressor not available")

        # Create data that will trigger extended matches
        # "tamp" * 1024 = 4096 bytes of repeated pattern
        # This is much longer than min_pattern_size + 13, so it should use extended matches
        test_data = b"tamp" * 1024

        # Compress with Python compressor (v2 enabled)
        with BytesIO() as py_f:
            py_c = PyCompressor(py_f, v2=True)
            py_c.write(test_data)
            py_c.flush()
            py_compressed = py_f.getvalue()

        # Compress with C compressor (v2 enabled)
        with BytesIO() as c_f:
            c_c = CCompressor(c_f, v2=True)
            c_c.write(test_data)
            c_c.flush()
            c_compressed = c_f.getvalue()

        # Verify they produce identical output
        self.assertEqual(
            py_compressed,
            c_compressed,
            msg=f"Python and C compressors produced different output.\n"
            f"Python: {py_compressed.hex()}\n"
            f"C:      {c_compressed.hex()}",
        )

        # Also verify both can decompress correctly
        with BytesIO(py_compressed) as f:
            d = PyDecompressor(f)
            self.assertEqual(d.read(), test_data)

        with BytesIO(c_compressed) as f:
            d = CDecompressor(f)
            self.assertEqual(d.read(), test_data)

    def test_rle_at_end_python_compress_c_decompress(self):
        """Test C decompressor with RLE sequence at the end of stream."""
        if PyCompressor is None or CDecompressor is None:
            self.skipTest("Python compressor or C decompressor not available")

        test_cases = [
            b"Hello World!" + b"\x00" * 500,  # Mixed + repeated bytes
            b"X" * 2000,  # Pure RLE
            b"ABCD" + b"Z" * 4,  # Short RLE at end
        ]

        for write_token in (True, False):
            for test_data in test_cases:
                with self.subTest(write_token=write_token, data_len=len(test_data)):
                    with BytesIO() as compress_f:
                        py_compressor = PyCompressor(compress_f, v2=True)
                        py_compressor.write(test_data)
                        py_compressor.flush(write_token=write_token)
                        compressed_data = compress_f.getvalue()

                    with BytesIO(compressed_data) as decompress_f:
                        c_decompressor = CDecompressor(decompress_f)
                        decompressed_data = c_decompressor.read()

                    self.assertEqual(decompressed_data, test_data)


if __name__ == "__main__":
    unittest.main()
