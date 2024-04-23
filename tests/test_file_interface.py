import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

try:
    import micropython
except ImportError:
    micropython = None

modules = []

import tamp

modules.append(tamp)

try:
    import tamp_native

    modules.append(tamp_native)
except ImportError:
    pass


class TestFileInterface(unittest.TestCase):
    def test_open_wb(self):
        for module in modules:
            with self.subTest(module=module), TemporaryDirectory() as tmp_dir:
                tmp_dir = Path(tmp_dir)
                fn = tmp_dir / "file.tamp"
                f = module.open(fn, "wb")
                self.assertIsInstance(f, module.Compressor)
                f.close()

    def test_open_wb_then_rb(self):
        for module in modules:
            with self.subTest(module=module), TemporaryDirectory() as tmp_dir:
                tmp_dir = Path(tmp_dir)
                fn = tmp_dir / "file.tamp"

                f = module.open(fn, "wb")
                f.close()

                f = module.open(fn, "rb")
                self.assertIsInstance(f, module.Decompressor)
                f.close()

    def test_open_context_manager_read_write(self):
        for module in modules:
            with self.subTest(module=module), TemporaryDirectory() as tmp_dir:
                tmp_dir = Path(tmp_dir)
                fn = tmp_dir / "file.tamp"

                test_string = b"test string is best string"
                with module.open(fn, "wb") as f:
                    f.write(test_string)

                with module.open(fn, "rb") as f:
                    actual = f.read()

                assert test_string == actual

    def test_encoding(self):
        for module in modules:
            with self.subTest(module=module), TemporaryDirectory() as tmp_dir:
                tmp_dir = Path(tmp_dir)
                fn = tmp_dir / "file.tamp"

                test_string = "test string is best string"
                with module.open(fn, "w") as f:
                    f.write(test_string)

                with module.open(fn, "r") as f:
                    actual = f.read()

                assert test_string == actual

    def test_bad_modes(self):
        for module in modules:
            with self.subTest(module=module):
                with self.assertRaises(ValueError):
                    module.open(None, "abc")  # type: ignore[reportGeneralTypeIssues]

                with self.assertRaises(ValueError):
                    module.open(None, "rw")  # type: ignore[reportGeneralTypeIssues]
