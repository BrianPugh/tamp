import unittest
from io import BytesIO

try:
    import micropython
except ImportError:
    micropython = None

Decompressors = []
decompresses = []

if micropython is None:
    from tamp.decompressor import Decompressor as PyDecompressor
    from tamp.decompressor import decompress as py_decompress

    Decompressors.append(PyDecompressor)
    decompresses.append(py_decompress)

    try:
        from tamp._c_decompressor import Decompressor as CDecompressor
        from tamp._c_decompressor import decompress as c_decompress

        Decompressors.append(CDecompressor)
        decompresses.append(c_decompress)
    except ImportError:
        pass

else:
    try:
        from tamp_native import Decompressor as NativeDecompressor
        from tamp_native import decompress as native_decompress

        Decompressors.append(NativeDecompressor)
        decompresses.append(native_decompress)
    except ImportError:
        pass


class TestDecompressor(unittest.TestCase):
    def test_decompressor_basic(self):
        for Decompressor in Decompressors:
            with self.subTest(Decompressor=Decompressor):
                expected = b"foo foo foo"

                compressed = bytes(
                    # fmt: off
                    [
                        0b010_11_00_0,  # header (window_bits=10, literal_bits=8)
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

                with BytesIO(compressed) as f:
                    decompressor = Decompressor(f)
                    actual = decompressor.read()

                self.assertEqual(actual, expected)

    def test_decompressor_restricted_read_size(self):
        for Decompressor in Decompressors:
            with self.subTest(Decompressor=Decompressor):
                compressed = bytes(
                    # fmt: off
                    [
                        0b010_11_00_0,  # header (window_bits=10, literal_bits=8)
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

                with BytesIO(compressed) as f:
                    decompressor = Decompressor(f)
                    self.assertEqual(decompressor.read(4), b"foo ")
                    self.assertEqual(decompressor.read(2), b"fo")
                    self.assertEqual(decompressor.read(-1), b"o foo")

    def test_decompressor_flushing(self):
        for decompress in decompresses:
            with self.subTest(decompress=decompress):
                compressed = bytes(
                    # fmt: off
                    [
                        0b010_11_00_0,  # header (window_bits=10, literal_bits=8)
                        0b1_0101000,  # literal 'Q'
                        0b1_0_101010,  # FLUSH_CODE
                        0b11_000000,  # FLUSH CONTINUE
                        0b1_0101011,  # literal 'W'
                        0b1_0_101010,  # FLUSH_CODE
                        0b11_000000,  # FLUSH CONTINUE
                    ]
                    # fmt: on
                )
                decoded = decompress(compressed)
                self.assertEqual(decoded, b"QW")

    def test_decompressor_missing_dict(self):
        for Decompressor in Decompressors:
            # fmt: off
            with self.subTest(Decompressor=Decompressor), \
                 self.assertRaises(ValueError), \
                 BytesIO(bytes([0b000_10_1_0_0])) as f:
                Decompressor(f)
            # fmt: on

    def test_decompressor_full_output_dst_immediately_after_src(self):
        # Decompressor's perspective of window
        # compressed data: b"z"
        #    * 0 write literal "a"   -> b"abcd"
        #    * 1 write pattern "abc"
        custom_dictionary = bytearray(1024)
        custom_dictionary_init = b"abcd"
        custom_dictionary[: len(custom_dictionary_init)] = custom_dictionary_init

        data = bytes(
            [
                # fmt: off
                # header (window_bits=10, literal_bits=8, custom)
                0b010_11_1_0_0,
                0b1_0110000,  # literal "a"
                0b1_0_11_0000,  # token "abc"
                0b000000_00,
                # 2-bit padding
                # fmt: on
            ]
        )

        for Decompressor in Decompressors:
            with self.subTest(Decompressor=Decompressor):
                with BytesIO(data) as f:
                    # Sanity check that without limiting output, it decompresses correctly.
                    decompressor = Decompressor(f, dictionary=bytearray(custom_dictionary))
                    self.assertEqual(decompressor.read(), b"aabc")

                with BytesIO(data) as f:
                    decompressor = Decompressor(f, dictionary=bytearray(custom_dictionary))
                    self.assertEqual(decompressor.read(1), b"a")
                    self.assertEqual(decompressor.read(1), b"a")
                    self.assertEqual(decompressor.read(1), b"b")
                    self.assertEqual(decompressor.read(1), b"c")

    def test_decompressor_partial_token(self):
        compressed = bytes(
            # fmt: off
            [
                0b010_11_00_0,  # header (window_bits=10, literal_bits=8)
                0b1_0110011,  # literal "f"
                0b0_0_0_00100,  # the pre-init buffer contains "oo" at index 131
                # size=2 -> 0b0
                # 131 -> 0b0010000011
                0b00011_1_00,  # literal " "
                0b100000_0_1,  # There is now "foo " at index 0
                0b000_00000,  # size=4 -> 0b1000
                ####################### - stream-break here
                0b00000_0_11,  # Just "foo" at index 0; size=3 -> 0b11
                0b00000000,  # index 0 -> 0b0000000000
                0b00_000000,  # 6 bits of zero-padding
            ]
            # fmt: on
        )

        expected = b"foo foo foo"

        for Decompressor in Decompressors:
            with self.subTest(Decompressor=Decompressor), BytesIO(compressed[:6]) as f:
                decompressor = Decompressor(f)
                read0 = decompressor.read()

                f.write(compressed[6:])
                f.seek(6)

                read1 = decompressor.read()
                self.assertEqual(read0 + read1, expected)

    def test_decompressor_extended_rle(self):
        """Decompress extended-format data containing an RLE token."""
        for Decompressor in Decompressors:
            with self.subTest(Decompressor=Decompressor):
                # 20 bytes of 'A': literal 'A' + RLE(count=19)
                compressed = bytes(
                    # fmt: off
                    [
                        0x5A,  # header: 010_11_0_1_0 (window=10, literal=8, extended=1)
                        0xA0,  # literal 'A' bits + start token flag
                        0xAA,  # RLE huffman sym12
                        0xB1,  # count upper=sym1, lower=0001, pad
                    ]
                    # fmt: on
                )
                with BytesIO(compressed) as f:
                    decompressor = Decompressor(f)
                    actual = decompressor.read()
                self.assertEqual(actual, b"A" * 20)

    def test_decompressor_extended_rle_short(self):
        """Decompress extended RLE with small count (count=4)."""
        for Decompressor in Decompressors:
            with self.subTest(Decompressor=Decompressor):
                compressed = bytes(
                    # fmt: off
                    [
                        0x5A,  # header: 010_11_0_1_0 (window=10, literal=8, extended=1)
                        0xA1,  # literal 'B' bits + start token flag
                        0x2A,  # ...0 from B, token flag, RLE huffman partial
                        0x84,  # ...huffman end, count upper=sym0, lower=0010, pad
                    ]
                    # fmt: on
                )
                with BytesIO(compressed) as f:
                    decompressor = Decompressor(f)
                    actual = decompressor.read()
                self.assertEqual(actual, b"B" * 5)

    def test_decompressor_extended_match(self):
        """Decompress extended-format data containing an extended match token."""
        for Decompressor in Decompressors:
            with self.subTest(Decompressor=Decompressor):
                # 14-byte match at index 0 in custom dictionary
                pattern = b"abcdefghijklmn"
                dictionary = bytearray(1 << 8)
                dictionary[: len(pattern)] = pattern

                compressed = bytes(
                    # fmt: off
                    [
                        0x1E,  # header: 000_11_1_1_0 (window=8, literal=8, custom=1, extended=1)
                        0x4E,  # ext_match token + count upper=sym0
                        0x00,  # count lower=000, position=0 partial
                        0x00,  # position end, padding
                    ]
                    # fmt: on
                )
                with BytesIO(compressed) as f:
                    decompressor = Decompressor(f, dictionary=dictionary)
                    actual = decompressor.read()
                self.assertEqual(actual, pattern)

    def test_decompressor_extended_rle_restricted_read(self):
        """Decompress RLE data with restricted read sizes to test partial output."""
        for Decompressor in Decompressors:
            with self.subTest(Decompressor=Decompressor):
                compressed = bytes(
                    # fmt: off
                    [
                        0x5A,  # header: 010_11_0_1_0 (window=10, literal=8, extended=1)
                        0xA0,  # literal 'A' bits + start token flag
                        0xAA,  # RLE huffman sym12
                        0xB1,  # count upper=sym1, lower=0001, pad
                    ]
                    # fmt: on
                )
                with BytesIO(compressed) as f:
                    decompressor = Decompressor(f)
                    part1 = decompressor.read(5)
                    part2 = decompressor.read(10)
                    part3 = decompressor.read(-1)
                self.assertEqual(part1 + part2 + part3, b"A" * 20)

    def test_decompressor_readinto(self):
        for Decompressor in Decompressors:
            with self.subTest(Decompressor=Decompressor):
                expected = b"foo foo foo"

                compressed = bytes(
                    # fmt: off
                    [
                        0b010_11_00_0,  # header (window_bits=10, literal_bits=8)
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

                actual_buf = bytearray(1024)
                with BytesIO(compressed) as f:
                    decompressor = Decompressor(f)
                    actual_len = decompressor.readinto(actual_buf)

                self.assertEqual(actual_len, len(expected))
                self.assertEqual(actual_buf[:actual_len], expected)
