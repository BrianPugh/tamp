import sys
from pathlib import Path
from typing import Annotated, Literal, Optional

from cyclopts import App, Parameter, validators

import tamp

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
    lazy_matching: bool = False,
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
    lazy_matching: bool
        Use roughly 50% more cpu to get 0~2% better compression.
    implementation: Optional[Literal["c", "python"]]
        Explicitly specify which implementation to use (c or python). Defaults to auto-detection.
    """
    input_bytes = read(input_)
    compress_fn = get_compress_implementation(implementation)
    output_bytes = compress_fn(
        input_bytes,
        window=window,
        literal=literal,
        lazy_matching=lazy_matching,
    )
    write(output, output_bytes)


@app.command()
def decompress(
    input_: Annotated[Optional[Path], Parameter(name=["--input", "-i"])] = None,
    output: Annotated[Optional[Path], Parameter(name=["--output", "-o"])] = None,
    *,
    implementation: ImplementationType = None,
):
    """Decompress an input file or stream.

    Parameters
    ----------
    input_: Optional[Path]
        Input file to decompress. Defaults to stdin.
    output: Optional[Path]
        Output decompressed file. Defaults to stdout.
    implementation: Optional[Literal["c", "python"]]
        Explicitly specify which implementation to use (c or python). Defaults to auto-detection.
    """
    input_bytes = read(input_)
    decompress_fn = get_decompress_implementation(implementation)
    output_bytes = decompress_fn(input_bytes)
    write(output, output_bytes)


def run_app():
    app()
