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
from tamp._c import Compressor, compress


class TestCompressor(unittest.TestCase):
    def test_compressor_default(self):
        test_string = b"foo foo foo"

        expected = bytes(
            # fmt: off
            [
                0b010_11_0_0_0,  # header (window_bits=10, literal_bits=8)
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

        bytes_written = 0
        with io.BytesIO() as f:
            compressor = Compressor(f)
            bytes_written += compressor.write(test_string)
            bytes_written += compressor.flush(write_token=False)

            f.seek(0)
            actual = f.read()
        self.assertEqual(actual, expected)
        self.assertEqual(bytes_written, len(expected) - 1)  # Not including header

    def test_compressor_input_buffer(self):
        expected = bytes(
            # fmt: off
            [
                0b010_11_0_0_0,  # header (window_bits=10, literal_bits=8)
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

        with io.BytesIO() as f:
            compressor = Compressor(f)
            compressor.write(b"f")
            compressor.write(b"oo")
            compressor.write(b" fo")
            compressor.write(b"o foo")
            compressor.flush(write_token=False)

            f.seek(0)
            actual = f.read()
        self.assertEqual(actual, expected)

    def test_compressor_7bit(self):
        test_string = b"foo foo foo"

        expected = bytes(
            # fmt: off
            [
                0b010_10_0_0_0,  # header (window_bits=10, literal_bits=7)
                0b1_1100110,    # literal "f"
                0b0_0_001000,   # the pre-init buffer contains "oo " at index 131
                                # size=2 -> 0b0
                                # 131 -> 0b0010000011
                0b0011_1_010,   # literal " "
                0b0000_0_100,   # size=4 -> 0b1000
                0b0_0000000,
                0b000_0_11_00,  # Just "foo" at index 0; size=3 -> 0b11
                0b000000000,    # index 0 -> 0b0000000000
                # no padding!
            ]
            # fmt: on
        )
        with io.BytesIO() as f:
            compressor = Compressor(f, literal=7)
            compressor.write(test_string)
            compressor.flush(write_token=False)

            f.seek(0)
            actual = f.read()
        self.assertEqual(actual, expected)

    def test_compressor_predefined_dictionary(self):
        test_string = b"foo foo foo"

        init_string = b"foo foo foo"
        dictionary = bytearray(1 << 8)
        dictionary[: len(init_string)] = init_string

        expected = bytes(
            # fmt: off
            [
                0b000_10_1_0_0,  # header (window_bits=8, literal_bits=7, dictionary provided)
                0b0_1010100,     # match-size 11
                0b00000000,      # At index 0
                # no padding!
            ]
            # fmt: on
        )

        with io.BytesIO() as f:
            compressor = Compressor(f, window=8, literal=7, dictionary=dictionary)
            compressor.write(test_string)
            compressor.flush(write_token=False)

            f.seek(0)
            actual = f.read()
        self.assertEqual(actual, expected)

    def test_compressor_predefined_dictionary_incorrect_size(self):
        dictionary = bytearray(1 << 8)
        with io.BytesIO() as f, self.assertRaises(ValueError):
            Compressor(f, window=9, literal=7, dictionary=dictionary)

    def test_oob_2_byte_pattern(self):
        """Viper implementation had a bug where a pattern of length 2 could be detected at the end of a string (going out of bounds by 1 byte)."""
        test_string_extended = bytearray(b"Q\x00Q\x00")
        test_string = memoryview(test_string_extended)[:3]  # b"Q\x00Q"

        with io.BytesIO() as f:
            compressor = Compressor(f)
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
        with io.BytesIO() as f:
            compressor = Compressor(f, literal=7)

            with self.assertRaises(ExcessBitsError):
                compressor.write(b"\xFF")
                compressor.flush()

    def test_single_shot_compress_text(self):
        expected = bytes(
            # fmt: off
            [
                0b010_11_0_0_0,  # header (window_bits=10, literal_bits=8)
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
        self.assertEqual(compress("foo foo foo"), expected)
