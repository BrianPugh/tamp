import tempfile
import unittest
from pathlib import Path

try:
    import micropython
except ImportError:
    micropython = None

try:
    from typer.testing import CliRunner

    from tamp.cli.main import app
except ImportError:
    pass
else:
    runner = CliRunner()

compressed_foo_foo_foo = bytes(
    # fmt: off
    [
        0b010_11_00_0,  # header (window_bits=10, literal_bits=8)
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


@unittest.skipIf(micropython is not None, "not running cpython")
class TestCli(unittest.TestCase):
    def test_compress_file_to_stdout(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_dir = Path(tmp_dir)
            test_file = tmp_dir / "test_input.bin"
            test_file.write_bytes(b"foo foo foo")
            result = runner.invoke(app, ["compress", str(test_file)])
            self.assertEqual(result.exit_code, 0)
            self.assertEqual(result.stdout_bytes, compressed_foo_foo_foo)

    def test_compress_stdin_to_stdout(self):
        result = runner.invoke(app, ["compress"], input="foo foo foo")
        self.assertEqual(result.exit_code, 0)
        self.assertEqual(result.stdout_bytes, compressed_foo_foo_foo)

    def test_decompress_file_to_stdout(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_dir = Path(tmp_dir)
            test_file = tmp_dir / "test_input.tamp"
            test_file.write_bytes(compressed_foo_foo_foo)
            result = runner.invoke(app, ["decompress", str(test_file)])
            self.assertEqual(result.exit_code, 0)
            self.assertEqual(result.stdout, "foo foo foo")

    def test_decompress_stdin_to_stdout(self):
        result = runner.invoke(app, ["decompress"], input=compressed_foo_foo_foo)
        self.assertEqual(result.exit_code, 0)
        self.assertEqual(result.stdout, "foo foo foo")

    def test_decompress_stdin_to_file(self):
        with tempfile.TemporaryDirectory() as tmp_dir:
            tmp_dir = Path(tmp_dir)
            test_file = tmp_dir / "test_output.txt"

            result = runner.invoke(app, ["decompress", "-o", str(test_file)], input=compressed_foo_foo_foo)
            self.assertEqual(result.exit_code, 0)
            self.assertEqual(test_file.read_text(), "foo foo foo")

    def test_version(self):
        result = runner.invoke(app, ["--version"])
        self.assertEqual(result.exit_code, 0)
