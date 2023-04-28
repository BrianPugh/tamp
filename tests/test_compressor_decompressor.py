import random
import unittest
from io import BytesIO

from tamp import Compressor, Decompressor

tale_of_two_cities = b"It was the best of times, it was the worst of times, it was the age of wisdom, it was the age of foolishness, it was the epoch of belief, it was the epoch of incredulity, it was the season of Light, it was the season of Darkness, it was the spring of hope, it was the winter of despair, we had everything before us, we had nothing before us, we were all going direct to Heaven, we were all going direct the other way - in short, the period was so far like the present period, that some of its noisiest authorities insisted on its being received, for good or for evil, in the superlative degree of comparison only."


class TestCompressorAndDecompressor(unittest.TestCase):
    def _autotest(self, num_bytes, n_bits, compressor_kwargs=None):
        if compressor_kwargs is None:
            compressor_kwargs = {}

        data = bytearray(random.randint(0, (1 << n_bits) - 1) for x in range(num_bytes))

        with BytesIO() as f:
            c = Compressor(f, **compressor_kwargs)
            c.write(data)
            c.flush()

            f.seek(0)
            d = Decompressor(f)
            actual = d.read()

        self.assertEqual(actual, data)

        data = bytearray(1 for _ in range(num_bytes))
        with BytesIO() as f:
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
        with BytesIO() as f:
            c = Compressor(f, window=8)
            c.write(tale_of_two_cities)
            c.flush()

            f.seek(0)
            d = Decompressor(f)
            actual = d.read()

            assert actual == tale_of_two_cities


if __name__ == "__main__":
    unittest.main()
