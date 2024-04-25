import io
import random
import unittest

from tamp.compressor import _BitWriter
from tamp.decompressor import _BitReader


class TestBitWriterAndReader(unittest.TestCase):
    def test_auto_bit_writer_and_reader(self):
        # Generate a list of random chunks of bits (1~16 bits)
        num_chunks = 1000
        n_bits = [random.randint(1, 16) for _ in range(num_chunks)]
        data = []
        for n_bit in n_bits:
            mask = (1 << n_bit) - 1
            data.append(random.randint(0, 1 << 32) & mask)
        chunks = list(zip(data, n_bits))

        # Write the chunks of bits using BitWriter
        with io.BytesIO() as f:
            writer = _BitWriter(f)
            for bits, num_bits in chunks:
                writer.write(bits, num_bits)
            writer.flush(write_token=False)

            # Read the chunks of bits back using BitReader
            f.seek(0)
            reader = _BitReader(f)
            for original_bits, num_bits in chunks:
                read_bits = reader.read(num_bits)
                self.assertEqual(read_bits, original_bits)

    def test_writer_correct_size_no_flush_token(self):
        for i in range(1, 8 + 1):
            with io.BytesIO() as f:
                writer = _BitWriter(f)
                writer.write(0xFFFF, i)
                writer.flush(write_token=False)

                self.assertEqual(f.tell(), 1)
        for i in range(9, 16 + 1):
            with io.BytesIO() as f:
                writer = _BitWriter(f)
                writer.write(0xFFFF, i)
                writer.flush(write_token=False)

                self.assertEqual(f.tell(), 2)


class TestHuffmanReader(unittest.TestCase):
    def test_always_valid(self):
        """There should never be an instance where 8bits have been read and
        no huffman code has been decoded. This verifies that.
        """
        random_bytes = bytes(random.randint(0, 255) for _ in range(1024 * 1024))
        with io.BytesIO(random_bytes) as f:
            reader = _BitReader(f)
            reader.read_huffman()
