import sys
from pathlib import Path
from typing import Annotated, Literal, Optional

from cyclopts import App, Parameter, validators

import tamp
from tamp.cli.build_dictionary import build_dictionary_cli

app = App(help="Compress/Decompress data in Tamp format.")

ImplementationType = Optional[Literal["c", "python"]]


def get_compress_implementation(impl_name: Optional[str]):
    """Get the compress function based on the implementation name.

    Parameters
    ----------
    impl_name : Optional[str]
        Name of the implementation ('c', 'python', or None for default)

    Returns
    -------
    callable
        The requested compress function
    """
    if impl_name is None:
        # Use default auto-detection
        return tamp.compress

    impl_name = impl_name.lower()

    if impl_name == "c":
        try:
            from tamp._c_compressor import compress

            return compress
        except ImportError:
            raise ImportError(
                "C implementation not available. Please ensure the C extensions are compiled "
                "by running: poetry run python build.py build_ext --inplace"
            )
    elif impl_name == "python":
        from tamp.compressor import compress

        return compress
    else:
        raise ValueError(f"Unknown implementation: {impl_name}. Valid options are 'c' or 'python'")


def get_decompress_implementation(impl_name: Optional[str]):
    """Get the decompress function based on the implementation name.

    Parameters
    ----------
    impl_name : Optional[str]
        Name of the implementation ('c', 'python', or None for default)

    Returns
    -------
    callable
        The requested decompress function
    """
    if impl_name is None:
        # Use default auto-detection
        return tamp.decompress

    impl_name = impl_name.lower()

    if impl_name == "c":
        try:
            from tamp._c_decompressor import decompress

            return decompress
        except ImportError:
            raise ImportError(
                "C implementation not available. Please ensure the C extensions are compiled "
                "by running: poetry run python build.py build_ext --inplace"
            )
    elif impl_name == "python":
        from tamp.decompressor import decompress

        return decompress
    else:
        raise ValueError(f"Unknown implementation: {impl_name}. Valid options are 'c' or 'python'")


def read(input_: Optional[Path]) -> bytes:
    data = sys.stdin.buffer.read() if input_ is None else input_.read_bytes()
    if not data:
        raise ValueError("No data provided.")
    return data


def load_dictionary(path: Path, window: int, literal: int, extended: bool) -> bytearray:
    """Load a dictionary file, expanding to window size if needed.

    If the file is smaller than the window, it is treated as raw effective
    bytes: initialize_dictionary fills the buffer first, then the file
    contents are copied to the end.
    """
    raw = path.read_bytes()
    window_size = 1 << window
    if len(raw) == window_size:
        return bytearray(raw)
    if len(raw) > window_size:
        raise ValueError(f"Dictionary file ({len(raw)} bytes) is larger than window size ({window_size} bytes).")
    dictionary = tamp.initialize_dictionary(window_size, literal=literal if extended else 8)
    dictionary[-len(raw) :] = raw
    return dictionary


def write(output: Optional[Path], data: bytes):
    if output is None:
        sys.stdout.buffer.write(data)
    else:
        output.write_bytes(data)


@app.command()
def compress(
    input_: Annotated[Optional[Path], Parameter(name=["--input", "-i"])] = None,
    output: Annotated[Optional[Path], Parameter(name=["--output", "-o"])] = None,
    *,
    window: Annotated[
        int,
        Parameter(
            name=["--window", "-w"],
            validator=validators.Number(gte=8, lte=15),
        ),
    ] = 10,
    literal: Annotated[
        int,
        Parameter(
            name=["--literal", "-l"],
            validator=validators.Number(gte=5, lte=8),
        ),
    ] = 8,
    dictionary: Annotated[Optional[Path], Parameter(name=["--dictionary", "-d"])] = None,
    lazy_matching: bool = False,
    extended: bool = True,
    implementation: ImplementationType = None,
):
    """Compress an input file or stream.

    Parameters
    ----------
    input_: Optional[Path]
        Input file to compress. Defaults to stdin.
    output: Optional[Path]
        Output compressed file. Defaults to stdout.
    window: int
        Number of bits used to represent the dictionary window.
    literal: int
        Number of bits used to represent a literal.
    dictionary: Optional[Path]
        Path to a custom initialization dictionary binary file.
    lazy_matching: bool
        Use roughly 50% more cpu to get 0~2% better compression.
    extended: bool
        Use extended compression format (RLE, extended match encoding).
    implementation: Optional[Literal["c", "python"]]
        Explicitly specify which implementation to use (c or python). Defaults to auto-detection.
    """
    input_bytes = read(input_)
    compress_fn = get_compress_implementation(implementation)

    kwargs = {
        "window": window,
        "literal": literal,
        "lazy_matching": lazy_matching,
        "extended": extended,
    }
    if dictionary is not None:
        kwargs["dictionary"] = load_dictionary(dictionary, window, literal, extended)

    output_bytes = compress_fn(input_bytes, **kwargs)
    write(output, output_bytes)


@app.command()
def decompress(
    input_: Annotated[Optional[Path], Parameter(name=["--input", "-i"])] = None,
    output: Annotated[Optional[Path], Parameter(name=["--output", "-o"])] = None,
    *,
    dictionary: Annotated[Optional[Path], Parameter(name=["--dictionary", "-d"])] = None,
    window: Annotated[
        int,
        Parameter(
            name=["--window", "-w"],
            validator=validators.Number(gte=8, lte=15),
        ),
    ] = 10,
    literal: Annotated[
        int,
        Parameter(
            name=["--literal", "-l"],
            validator=validators.Number(gte=5, lte=8),
        ),
    ] = 8,
    extended: bool = True,
    implementation: ImplementationType = None,
):
    """Decompress an input file or stream.

    Parameters
    ----------
    input_: Optional[Path]
        Input file to decompress. Defaults to stdin.
    output: Optional[Path]
        Output decompressed file. Defaults to stdout.
    dictionary: Optional[Path]
        Path to a custom initialization dictionary binary file.
        If smaller than the window size, it is treated as raw effective
        bytes and expanded with initialize_dictionary.
    window: int
        Number of bits used to represent the dictionary window.
        Only needed when using a raw (undersized) dictionary file.
    literal: int
        Number of bits used to represent a literal.
        Only needed when using a raw (undersized) dictionary file.
    extended: bool
        Extended compression format flag.
        Only needed when using a raw (undersized) dictionary file.
    implementation: Optional[Literal["c", "python"]]
        Explicitly specify which implementation to use (c or python). Defaults to auto-detection.
    """
    input_bytes = read(input_)
    decompress_fn = get_decompress_implementation(implementation)

    kwargs = {}
    if dictionary is not None:
        kwargs["dictionary"] = load_dictionary(dictionary, window, literal, extended)

    output_bytes = decompress_fn(input_bytes, **kwargs)
    write(output, output_bytes)


app.command(build_dictionary_cli, name="build-dictionary")


def run_app():
    app()
