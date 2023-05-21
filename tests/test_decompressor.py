import unittest
from io import BytesIO

try:
    import micropython
except ImportError:
    micropython = None

from tamp.decompressor import Decompressor as PyDecompressor
from tamp.decompressor import decompress as py_decompress

if micropython is None:
    ViperDecompressor = None
    viper_decompress = None
else:
    from tamp.decompressor_viper import Decompressor as ViperDecompressor
    from tamp.decompressor_viper import decompress as viper_decompress

try:
    from tamp._c_decompressor import Decompressor as CDecompressor
    from tamp._c_decompressor import decompress as c_decompress
except ImportError:
    CDecompressor = None
    c_decompress = None

Decompressors = (PyDecompressor, CDecompressor, ViperDecompressor)
decompresses = (py_decompress, c_decompress, viper_decompress)


class TestDecompressor(unittest.TestCase):
    def test_decompressor_basic(self):
        for Decompressor in Decompressors:
            if Decompressor is None:
                continue
            with self.subTest(Decompressor=Decompressor):
                expected = b"foo foo foo"

                compressed = bytes(
                    # fmt: off
                    [
                        0b010_11_00_0,  # header (window_bits=10, literal_bits=8)
                        0b1_0110011,    # literal "f"
                        0b0_0_0_00100,  # the pre-init buffer contains "oo" at index 131
                                        # size=2 -> 0b0
                                        # 131 -> 0b0010000011
                        0b00011_1_00,   # literal " "
                        0b100000_0_1,   # There is now "foo " at index 0
                        0b000_00000,    # size=4 -> 0b1000
                        0b00000_0_11,   # Just "foo" at index 0; size=3 -> 0b11
                        0b00000000,     # index 0 -> 0b0000000000
                        0b00_000000,    # 6 bits of zero-padding
                    ]
                    # fmt: on
                )

                with BytesIO(compressed) as f:
                    decompressor = Decompressor(f)
                    actual = decompressor.read()

                self.assertEqual(actual, expected)

    def test_decompressor_restricted_read_size(self):
        for Decompressor in Decompressors:
            if Decompressor is None:
                continue
            with self.subTest(Decompressor=Decompressor):
                compressed = bytes(
                    # fmt: off
                    [
                        0b010_11_00_0,  # header (window_bits=10, literal_bits=8)
                        0b1_0110011,    # literal "f"
                        0b0_0_0_00100,  # the pre-init buffer contains "oo" at index 131
                                        # size=2 -> 0b0
                                        # 131 -> 0b0010000011
                        0b00011_1_00,   # literal " "
                        0b100000_0_1,   # There is now "foo " at index 0
                        0b000_00000,    # size=4 -> 0b1000
                        0b00000_0_11,   # Just "foo" at index 0; size=3 -> 0b11
                        0b00000000,     # index 0 -> 0b0000000000
                        0b00_000000,    # 6 bits of zero-padding
                    ]
                    # fmt: on
                )

                with BytesIO(compressed) as f:
                    decompressor = Decompressor(f)
                    self.assertEqual(decompressor.read(4), b"foo ")
                    self.assertEqual(decompressor.read(2), b"fo")
                    self.assertEqual(decompressor.read(-1), b"o foo")

    def test_decompressor_flushing(self):
        for decompress in decompresses:
            if decompress is None:
                continue
            with self.subTest(decompress=decompress):
                compressed = bytes(
                    # fmt: off
                    [
                        0b010_11_00_0,  # header (window_bits=10, literal_bits=8)
                        0b1_0101000,  # literal 'Q'
                        0b1_0_101010,  # FLUSH_CODE
                        0b11_000000,   # FLUSH CONTINUE
                        0b1_0101011,  # literal 'W'
                        0b1_0_101010,  # FLUSH_CODE
                        0b11_000000,   # FLUSH CONTINUE

                    ]
                    # fmt: on
                )
                decoded = decompress(compressed)
                self.assertEqual(decoded, b"QW")

    def test_decompressor_missing_dict(self):
        for Decompressor in Decompressors:
            if Decompressor is None:
                continue
            with self.subTest(Decompressor=Decompressor), self.assertRaises(ValueError), BytesIO(
                bytes([0b000_10_1_0_0])
            ) as f:
                Decompressor(f)
