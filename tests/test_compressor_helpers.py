import unittest

from tamp import bit_size, compute_min_pattern_size


class TestCompressorHelpers(unittest.TestCase):
    def test_bit_size(self):
        self.assertEqual(bit_size(0b11), 2)

    def test_bit_size_excess(self):
        self.assertEqual(bit_size(1 << 32), -1)

    def test_min_pattern_size(self):
        self.assertEqual(compute_min_pattern_size(window=10, literal=8), 2)
        self.assertEqual(compute_min_pattern_size(window=15, literal=5), 3)

    def test_min_pattern_size_out_of_range(self):
        with self.assertRaises(ValueError):
            compute_min_pattern_size(0, 0)
