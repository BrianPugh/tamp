"""Tamp compressor tests.

Our custom huffman size table:

   huffman_coding = {
            2: 0b0,
            3: 0b11,
            4: 0b1000,
            5: 0b1011,
            6: 0b10100,
            7: 0b100100,
            8: 0b100110,
            9: 0b101011,
           10: 0b1001011,
           11: 0b1010100,
           12: 0b10010100,
           13: 0b10010101,
           14: 0b10101010,
           15: 0b100111,
      "FLUSH": 0b10101011,
   }
"""

import io
import unittest

from tamp import ExcessBitsError

try:
    import micropython
except ImportError:
    micropython = None

Compressors = []
compresses = []
NativeExcessBitsError = ExcessBitsError

if micropython:
    try:
        from tamp_native import Compressor as NativeCompressor
        from tamp_native import ExcessBitsError as NativeExcessBitsError
        from tamp_native import compress as native_compress

        Compressors.append(NativeCompressor)
        compresses.append(native_compress)
    except ImportError:
        print("Skipping Native Module.")
else:
    from tamp.compressor import Compressor as PyCompressor
    from tamp.compressor import compress as py_compress

    Compressors.append(PyCompressor)
    compresses.append(py_compress)

    try:
        from tamp._c_compressor import Compressor as CCompressor
        from tamp._c_compressor import compress as c_compress

        Compressors.append(CCompressor)
        compresses.append(c_compress)
    except ImportError:
        pass


class TestCompressor(unittest.TestCase):
    def test_compressor_default(self):
        for Compressor in Compressors:
            with self.subTest(Compressor=Compressor):
                test_string = b"foo foo foo"

                expected = bytes(
                    # fmt: off
                    [
                        0b010_11_0_0_0,  # header (window_bits=10, literal_bits=8)
                        0b1_0110011,  # literal "f"
                        0b0_0_0_00100,  # the pre-init buffer contains "oo" at index 131
                        # size=2 -> 0b0
                        # 131 -> 0b0010000011
                        0b00011_1_00,  # literal " "
                        0b100000_0_1,  # There is now "foo " at index 0
                        0b000_00000,  # size=4 -> 0b1000
                        0b00000_0_11,  # Just "foo" at index 0; size=3 -> 0b11
                        0b00000000,  # index 0 -> 0b0000000000
                        0b00_000000,  # 6 bits of zero-padding
                    ]
                    # fmt: on
                )

                bytes_written = 0
                with io.BytesIO() as f:
                    compressor = Compressor(f, extended=False)
                    bytes_written += compressor.write(test_string)
                    bytes_written += compressor.flush(write_token=False)

                    f.seek(0)
                    actual = f.read()
                    compressor.close()
                self.assertEqual(actual, expected)
                self.assertEqual(bytes_written, len(expected))

                # Test Context Manager
                bytes_written = 0
                with io.BytesIO() as f, Compressor(f, extended=False) as compressor:
                    bytes_written += compressor.write(test_string)
                    bytes_written += compressor.flush(write_token=False)

                    f.seek(0)
                    actual = f.read()
                self.assertEqual(actual, expected)
                self.assertEqual(bytes_written, len(expected))

    def test_compressor_input_buffer(self):
        for Compressor in Compressors:
            with self.subTest(Compressor=Compressor):
                expected = bytes(
                    # fmt: off
                    [
                        0b010_11_0_0_0,  # header (window_bits=10, literal_bits=8)
                        0b1_0110011,  # literal "f"
                        0b0_0_0_00100,  # the pre-init buffer contains "oo" at index 131
                        # size=2 -> 0b0
                        # 131 -> 0b0010000011
                        0b00011_1_00,  # literal " "
                        0b100000_0_1,  # There is now "foo " at index 0
                        0b000_00000,  # size=4 -> 0b1000
                        0b00000_0_11,  # Just "foo" at index 0; size=3 -> 0b11
                        0b00000000,  # index 0 -> 0b0000000000
                        0b00_000000,  # 6 bits of zero-padding
                    ]
                    # fmt: on
                )

                with io.BytesIO() as f:
                    compressor = Compressor(f, extended=False)
                    compressor.write(b"f")
                    compressor.write(b"oo")
                    compressor.write(b" fo")
                    compressor.write(b"o foo")
                    compressor.flush(write_token=False)

                    f.seek(0)
                    actual = f.read()
                self.assertEqual(actual, expected)

    def test_compressor_7bit(self):
        for Compressor in Compressors:
            with self.subTest(Compressor=Compressor):
                test_string = b"foo foo foo"

                expected = bytes(
                    # fmt: off
                    [
                        0b010_10_0_0_0,  # header (window_bits=10, literal_bits=7)
                        0b1_1100110,  # literal "f"
                        0b0_0_001000,  # the pre-init buffer contains "oo " at index 131
                        # size=2 -> 0b0
                        # 131 -> 0b0010000011
                        0b0011_1_010,  # literal " "
                        0b0000_0_100,  # size=4 -> 0b1000
                        0b0_0000000,
                        0b000_0_11_00,  # Just "foo" at index 0; size=3 -> 0b11
                        0b000000000,  # index 0 -> 0b0000000000
                        # no padding!
                    ]
                    # fmt: on
                )
                with io.BytesIO() as f:
                    compressor = Compressor(f, literal=7, extended=False)
                    compressor.write(test_string)
                    compressor.flush(write_token=False)

                    f.seek(0)
                    actual = f.read()
                self.assertEqual(actual, expected)

    def test_compressor_predefined_dictionary(self):
        for Compressor in Compressors:
            with self.subTest(Compressor=Compressor):
                test_string = b"foo foo foo"

                init_string = b"foo foo foo"
                dictionary = bytearray(1 << 8)
                dictionary[: len(init_string)] = init_string

                expected = bytes(
                    # fmt: off
                    [
                        0b000_10_1_0_0,  # header (window_bits=8, literal_bits=7, dictionary provided)
                        0b0_1010100,  # match-size 11
                        0b00000000,  # At index 0
                        # no padding!
                    ]
                    # fmt: on
                )

                with io.BytesIO() as f:
                    compressor = Compressor(f, window=8, literal=7, dictionary=dictionary, extended=False)
                    compressor.write(test_string)
                    compressor.flush(write_token=False)

                    f.seek(0)
                    actual = f.read()
                self.assertEqual(actual, expected)

    def test_compressor_predefined_dictionary_incorrect_size(self):
        for Compressor in Compressors:
            with self.subTest(Compressor=Compressor):
                dictionary = bytearray(1 << 8)
                with io.BytesIO() as f, self.assertRaises(ValueError):
                    Compressor(f, window=9, literal=7, dictionary=dictionary)

    def test_oob_2_byte_pattern(self):
        """Viper implementation had a bug where a pattern of length 2 could be detected at the end of a string (going out of bounds by 1 byte)."""
        for Compressor in Compressors:
            with self.subTest(Compressor=Compressor):
                test_string_extended = bytearray(b"Q\x00Q\x00")
                test_string = memoryview(test_string_extended)[:3]  # b"Q\x00Q"

                with io.BytesIO() as f:
                    compressor = Compressor(f, extended=False)
                    compressor.write(test_string)
                    compressor.flush(write_token=False)

                    f.seek(0)
                    actual = f.read()

                # Q == 0b0101_0001
                expected = bytes(
                    [
                        0b010_11_00_0,
                        0b1_0101_000,
                        0b1_1_0000_00,
                        0b00_1_0101_0,
                        0b001_00000,
                    ]
                )
                assert actual == expected

    def test_excess_bits(self):
        for Compressor in Compressors:
            with self.subTest(Compressor=Compressor), io.BytesIO() as f:
                compressor = Compressor(f, literal=7, extended=False)

                with self.assertRaises((ExcessBitsError, NativeExcessBitsError)):
                    compressor.write(b"\xff")
                    compressor.flush()

    def test_single_shot_compress_text(self):
        for compress in compresses:
            with self.subTest(compress=compress):
                expected = bytes(
                    # fmt: off
                    [
                        0b010_11_0_0_0,  # header (window_bits=10, literal_bits=8)
                        0b1_0110011,  # literal "f"
                        0b0_0_0_00100,  # the pre-init buffer contains "oo" at index 131
                        # size=2 -> 0b0
                        # 131 -> 0b0010000011
                        0b00011_1_00,  # literal " "
                        0b100000_0_1,  # There is now "foo " at index 0
                        0b000_00000,  # size=4 -> 0b1000
                        0b00000_0_11,  # Just "foo" at index 0; size=3 -> 0b11
                        0b00000000,  # index 0 -> 0b0000000000
                        0b00_000000,  # 6 bits of zero-padding
                    ]
                    # fmt: on
                )
                self.assertEqual(compress("foo foo foo", extended=False), expected)

    def test_single_shot_compress_binary(self):
        for compress in compresses:
            with self.subTest(compress=compress):
                expected = bytes(
                    # fmt: off
                    [
                        0b010_11_0_0_0,  # header (window_bits=10, literal_bits=8)
                        0b1_0110011,  # literal "f"
                        0b0_0_0_00100,  # the pre-init buffer contains "oo" at index 131
                        # size=2 -> 0b0
                        # 131 -> 0b0010000011
                        0b00011_1_00,  # literal " "
                        0b100000_0_1,  # There is now "foo " at index 0
                        0b000_00000,  # size=4 -> 0b1000
                        0b00000_0_11,  # Just "foo" at index 0; size=3 -> 0b11
                        0b00000000,  # index 0 -> 0b0000000000
                        0b00_000000,  # 6 bits of zero-padding
                    ]
                    # fmt: on
                )
                self.assertEqual(compress(b"foo foo foo", extended=False), expected)

    def test_compressor_extended_header_bit(self):
        """Verify that extended=True sets header bit [1]."""
        for Compressor in Compressors:
            with self.subTest(Compressor=Compressor):
                with io.BytesIO() as f:
                    compressor = Compressor(f, window=10, literal=8, extended=True)
                    compressor.write(b"x")
                    compressor.flush(write_token=False)
                    f.seek(0)
                    header = f.read(1)[0]
                # extended bit is bit [1] of header byte
                self.assertEqual(header & 0x02, 0x02)

                with io.BytesIO() as f:
                    compressor = Compressor(f, window=10, literal=8, extended=False)
                    compressor.write(b"x")
                    compressor.flush(write_token=False)
                    f.seek(0)
                    header = f.read(1)[0]
                self.assertEqual(header & 0x02, 0x00)

    def test_compressor_extended_rle(self):
        """Bit-exact test: RLE encoding of repeated bytes with extended=True."""
        for Compressor in Compressors:
            with self.subTest(Compressor=Compressor):
                # 20 bytes of 'A' should produce: literal 'A' + RLE(count=19)
                # Bitstream after header: 1_01000001 0_10101010 11_0001 0
                #   literal 'A': flag=1, value=01000001
                #   RLE token: flag=0, huffman_sym12=10101010
                #   count=19: (19-2)=17, 17>>4=1 -> sym1=11, 17&0xF=1 -> 0001
                #   1 bit padding
                expected = bytes(
                    # fmt: off
                    [
                        0x5A,  # header: 010_11_0_1_0 (window=10, literal=8, extended=1)
                        0xA0,  # 1_0100000 | 0  (literal 'A' bits + start token flag)
                        0xAA,  # 10101010       (RLE huffman sym12)
                        0xB1,  # 11_0001_0      (count upper=sym1, lower=0001, pad)
                    ]
                    # fmt: on
                )
                with io.BytesIO() as f:
                    compressor = Compressor(f, extended=True)
                    compressor.write(b"A" * 20)
                    compressor.flush(write_token=False)
                    f.seek(0)
                    actual = f.read()
                self.assertEqual(actual, expected)

    def test_compressor_extended_rle_short(self):
        """Bit-exact test: short RLE (count=4) with extended=True."""
        for Compressor in Compressors:
            with self.subTest(Compressor=Compressor):
                # 5 bytes of 'B': literal 'B' + RLE(count=4)
                # count=4: (4-2)=2, 2>>4=0 -> sym0=0, 2&0xF=2 -> 0010
                expected = bytes(
                    # fmt: off
                    [
                        0x5A,  # header: 010_11_0_1_0 (window=10, literal=8, extended=1)
                        0xA1,  # 1_0100001 | 0  (literal 'B' bits + start token flag)
                        0x2A,  # 00101010       (partial: ...0 from B, token flag, RLE huffman)
                        0x84,  # 10000100       (...huffman end, count upper=0, lower=0010, pad)
                    ]
                    # fmt: on
                )
                with io.BytesIO() as f:
                    compressor = Compressor(f, extended=True)
                    compressor.write(b"B" * 5)
                    compressor.flush(write_token=False)
                    f.seek(0)
                    actual = f.read()
                self.assertEqual(actual, expected)

    def test_compressor_extended_match(self):
        """Bit-exact test: extended match token when match > min_pattern_size+11."""
        for Compressor in Compressors:
            with self.subTest(Compressor=Compressor):
                # Use a custom dictionary containing 'abcdefghijklmn' (14 bytes).
                # Input is the same 14 bytes -> match of size 14 at index 0.
                # min_pattern_size=2, threshold=2+11=13, so 14 triggers extended match.
                # Header: window=8, literal=8, custom=1, extended=1
                #   0b000_11_1_1_0 = 0x1E
                # Token: flag(0) + huffman_sym13(100111) = 7 bits
                # Extended count: 14-2-12=0, 0>>3=0 -> huffman_sym0(0), 0&0x7=0 -> 000
                # Position: index 0 -> 8 bits = 00000000
                pattern = b"abcdefghijklmn"
                dictionary = bytearray(1 << 8)
                dictionary[: len(pattern)] = pattern

                expected = bytes(
                    # fmt: off
                    [
                        0b000_11_1_1_0,  # header (window=8, literal=8, custom=1, extended=1)
                        0b0_100111_0,  # token flag=0; ext_match huffman=100111; count upper=0(sym 0)
                        0b000_00000,  # count lower=000; position=00000000 (index 0)
                        0b000_00000,  # ...000 end position; 5 bits padding
                    ]
                    # fmt: on
                )
                with io.BytesIO() as f:
                    compressor = Compressor(f, window=8, literal=8, dictionary=dictionary, extended=True)
                    compressor.write(pattern)
                    compressor.flush(write_token=False)
                    f.seek(0)
                    actual = f.read()
                self.assertEqual(actual, expected)

    def test_compressor_extended_match_larger(self):
        """Bit-exact test: extended match with count > 0."""
        for Compressor in Compressors:
            with self.subTest(Compressor=Compressor):
                # 16-byte pattern in dictionary -> extended match count = 16-2-12 = 2
                # 2>>3=0 -> huffman_sym0(0), 2&0x7=2 -> 010
                pattern = b"abcdefghijklmnop"  # 16 bytes
                dictionary = bytearray(1 << 8)
                dictionary[: len(pattern)] = pattern

                expected = bytes(
                    # fmt: off
                    [
                        0x1E,  # header: 000_11_1_1_0 (window=8, literal=8, custom=1, extended=1)
                        0x4E,  # 0_100111_0  (token, ext_match huffman, count upper=sym0)
                        0x40,  # 010_00000   (count lower=010, position=0 partial)
                        0x00,  # 000_00000   (position end, padding)
                    ]
                    # fmt: on
                )
                with io.BytesIO() as f:
                    compressor = Compressor(f, window=8, literal=8, dictionary=dictionary, extended=True)
                    compressor.write(pattern)
                    compressor.flush(write_token=False)
                    f.seek(0)
                    actual = f.read()
                self.assertEqual(actual, expected)

    def test_invalid_conf(self):
        for Compressor in Compressors:
            with self.subTest(Compressor=Compressor), io.BytesIO() as f:
                with self.assertRaises(ValueError):
                    Compressor(f, literal=4)
                with self.assertRaises(ValueError):
                    Compressor(f, window=16)
