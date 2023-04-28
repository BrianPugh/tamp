import unittest

import tamp.compressor
import tamp.decompressor
from tamp import initialize_dictionary

try:
    import micropython
except ImportError:
    micropython = None


class TestPseudoRandom(unittest.TestCase):
    def test_256_compressor_zero_seed(self):
        self.assertEqual(initialize_dictionary(256, seed=0), bytearray(256))

    def test_256_compressor_nonzero_seed(self):
        self.assertNotEqual(initialize_dictionary(256, seed=1), bytearray(256))

    def test_256_compressor(self):
        actual = tamp.compressor.initialize_dictionary(256)
        self.assertEqual(len(actual), 256)
        self.assertEqual(
            actual,
            b"\x00.//r.0. t>\n/>snas.trnr i\x00r/a\x00snat./.r\x00i o.s tneo>.as>\na.ta\x00 aa\x00\x00\x000oe ri\x00a>eatsi\n.\ni.str\n//snesr.ost<  \x00\ni\neoa\x00se0.o\n\n>aori>n0.>./.oonen0<\x00<r o\n\naas0< ai\n0\x00na\x00e><.\noas to \n></se>>ts/oreatinter.n0 >s\n/.e.><. r si<>/<san\x00ae t 0.r.o/0./a r/ttn nn.<re.t0 \x00r\x00ro",
        )

    def test_256_decompressor(self):
        actual = tamp.decompressor.initialize_dictionary(256)
        self.assertEqual(len(actual), 256)
        self.assertEqual(
            actual,
            b"\x00.//r.0. t>\n/>snas.trnr i\x00r/a\x00snat./.r\x00i o.s tneo>.as>\na.ta\x00 aa\x00\x00\x000oe ri\x00a>eatsi\n.\ni.str\n//snesr.ost<  \x00\ni\neoa\x00se0.o\n\n>aori>n0.>./.oonen0<\x00<r o\n\naas0< ai\n0\x00na\x00e><.\noas to \n></se>>ts/oreatinter.n0 >s\n/.e.><. r si<>/<san\x00ae t 0.r.o/0./a r/ttn nn.<re.t0 \x00r\x00ro",
        )

    @unittest.skipIf(micropython is None, "not running micropython")
    def test_256_compressor_viper(self):
        import tamp.compressor_viper

        actual = tamp.compressor_viper.initialize_dictionary(256)
        self.assertEqual(len(actual), 256)
        self.assertEqual(
            actual,
            b"\x00.//r.0. t>\n/>snas.trnr i\x00r/a\x00snat./.r\x00i o.s tneo>.as>\na.ta\x00 aa\x00\x00\x000oe ri\x00a>eatsi\n.\ni.str\n//snesr.ost<  \x00\ni\neoa\x00se0.o\n\n>aori>n0.>./.oonen0<\x00<r o\n\naas0< ai\n0\x00na\x00e><.\noas to \n></se>>ts/oreatinter.n0 >s\n/.e.><. r si<>/<san\x00ae t 0.r.o/0./a r/ttn nn.<re.t0 \x00r\x00ro",
        )

    @unittest.skipIf(micropython is None, "not running micropython")
    def test_256_decompressor_viper(self):
        import tamp.decompressor_viper

        actual = tamp.decompressor_viper.initialize_dictionary(256)
        self.assertEqual(len(actual), 256)
        self.assertEqual(
            actual,
            b"\x00.//r.0. t>\n/>snas.trnr i\x00r/a\x00snat./.r\x00i o.s tneo>.as>\na.ta\x00 aa\x00\x00\x000oe ri\x00a>eatsi\n.\ni.str\n//snesr.ost<  \x00\ni\neoa\x00se0.o\n\n>aori>n0.>./.oonen0<\x00<r o\n\naas0< ai\n0\x00na\x00e><.\noas to \n></se>>ts/oreatinter.n0 >s\n/.e.><. r si<>/<san\x00ae t 0.r.o/0./a r/ttn nn.<re.t0 \x00r\x00ro",
        )


if __name__ == "__main__":
    unittest.main()
