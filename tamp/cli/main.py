import sys
from pathlib import Path
from typing import Optional

from cyclopts import App, Parameter, validators
from typing_extensions import Annotated

import tamp

app = App(help="Compress/Decompress data in Tamp format.")


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
    """
    input_bytes = read(input_)
    output_bytes = tamp.compress(
        input_bytes,
        window=window,
        literal=literal,
    )
    write(output, output_bytes)


@app.command()
def decompress(
    input_: Annotated[Optional[Path], Parameter(name=["--input", "-i"])] = None,
    output: Annotated[Optional[Path], Parameter(name=["--output", "-o"])] = None,
):
    """Decompress an input file or stream.

    Parameters
    ----------
    input_: Optional[Path]
        Input file to decompress. Defaults to stdin.
    output: Optional[Path]
        Output decompressed file. Defaults to stdout.
    """
    input_bytes = read(input_)
    output_bytes = tamp.decompress(input_bytes)
    write(output, output_bytes)


def run_app():
    app()
