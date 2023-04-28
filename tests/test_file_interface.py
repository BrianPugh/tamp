import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import tamp


class TestFileInterface(unittest.TestCase):
    def test_open_wb(self):
        with TemporaryDirectory() as tmp_dir:
            tmp_dir = Path(tmp_dir)
            fn = tmp_dir / "file.tamp"
            f = tamp.open(fn, "wb")
            self.assertIsInstance(f, tamp.Compressor)

    def test_open_wb_then_rb(self):
        with TemporaryDirectory() as tmp_dir:
            tmp_dir = Path(tmp_dir)
            fn = tmp_dir / "file.tamp"

            f = tamp.open(fn, "wb")
            f.close()

            f = tamp.open(fn, "rb")
            self.assertIsInstance(f, tamp.Decompressor)

    def test_open_context_manager_read_write(self):
        with TemporaryDirectory() as tmp_dir:
            tmp_dir = Path(tmp_dir)
            fn = tmp_dir / "file.tamp"

            test_string = b"test string is best string"
            with tamp.open(fn, "wb") as f:
                f.write(test_string)

            with tamp.open(fn, "rb") as f:
                actual = f.read()

            assert test_string == actual

    def test_encoding(self):
        with TemporaryDirectory() as tmp_dir:
            tmp_dir = Path(tmp_dir)
            fn = tmp_dir / "file.tamp"

            test_string = "test string is best string"
            with tamp.open(fn, "w") as f:
                f.write(test_string)

            with tamp.open(fn, "r") as f:
                actual = f.read()

            assert test_string == actual
