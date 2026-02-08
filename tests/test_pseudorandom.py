import unittest

import tamp.compressor
import tamp.decompressor
from tamp import initialize_dictionary

modules = [tamp.compressor, tamp.decompressor]

try:
    import micropython
except ImportError:
    micropython = None

if micropython:
    try:
        import tamp_native

        modules.append(tamp_native)
    except ImportError:
        pass

_expected_256 = bytearray(
    b"\x00.//r.0. t>\n/>snas.trnr i\x00r/a\x00snat./.r\x00i o.s tneo>.as>\na.ta\x00 aa\x00\x00\x000oe ri\x00a>eatsi\n.\ni.str\n//snesr.ost<  \x00\ni\neoa\x00se0.o\n\n>aori>n0.>./.oonen0<\x00<r o\n\naas0< ai\n0\x00na\x00e><.\noas to \n></se>>ts/oreatinter.n0 >s\n/.e.><. r si<>/<san\x00ae t 0.r.o/0./a r/ttn nn.<re.t0 \x00r\x00ro",
)


class TestPseudoRandom(unittest.TestCase):
    def test_256_compressor_zero_seed(self):
        self.assertEqual(initialize_dictionary(256, seed=0), bytearray(256))

    def test_256_compressor_nonzero_seed(self):
        self.assertNotEqual(initialize_dictionary(256, seed=1), bytearray(256))

    def test_256_compressor_int(self):
        for module in modules:
            with self.subTest(module=module):
                actual = module.initialize_dictionary(256)
                assert isinstance(actual, bytearray)
                self.assertEqual(len(actual), 256)
                self.assertEqual(actual, _expected_256)

    def test_256_compressor_bytearray(self):
        for module in modules:
            with self.subTest(module=module):
                actual1 = bytearray(256)
                actual2 = module.initialize_dictionary(actual1)
                assert isinstance(actual1, bytearray)
                assert actual1 is actual2
                self.assertEqual(actual1, _expected_256)


if __name__ == "__main__":
    unittest.main()
