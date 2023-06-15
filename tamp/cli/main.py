import sys
from pathlib import Path
from typing import Callable, Optional

import typer
from typer import Argument, Option

import tamp

app = typer.Typer(no_args_is_help=True, pretty_exceptions_enable=False, add_completion=False)


def version_callback(value: bool):
    if not value:
        return
    print(tamp.__version__)
    raise typer.Exit()


@app.callback()
def common(
    version: bool = Option(
        None,
        "--version",
        "-v",
        callback=version_callback,
        help="Print tamp version.",
    ),
):
    pass


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
    input_path: Optional[Path] = Argument(
        None,
        exists=True,
        readable=True,
        dir_okay=False,
        show_default=False,
        help="Input file to compress or decompress. Defaults to stdin.",
    ),
    output_path: Optional[Path] = Option(
        None,
        "--output",
        "-o",
        exists=False,
        writable=True,
        dir_okay=True,
        show_default=False,
        help="Output file. Defaults to stdout.",
    ),
    window: int = Option(
        10,
        "-w",
        "--window",
        min=8,
        max=15,
        help="Number of bits used to represent the dictionary window.",
    ),
    literal: int = Option(
        8,
        "-l",
        "--literal",
        min=5,
        max=8,
        help="Number of bits used to represent a literal.",
    ),
):
    """Compress an input file or stream."""
    input_bytes = read(input_path)
    output_bytes = tamp.compress(
        input_bytes,
        window=window,
        literal=literal,
    )
    write(output_path, output_bytes)


@app.command()
def decompress(
    input_path: Optional[Path] = Argument(
        None,
        exists=True,
        readable=True,
        dir_okay=False,
        show_default=False,
        help="Input file. If not provided, reads from stdin.",
    ),
    output_path: Optional[Path] = Option(
        None,
        "--output",
        "-o",
        exists=False,
        writable=True,
        dir_okay=True,
        show_default=False,
        help="Output file. Defaults to stdout.",
    ),
):
    """Decompress an input file or stream."""
    input_bytes = read(input_path)
    output_bytes = tamp.decompress(input_bytes)
    write(output_path, output_bytes)


def run_app(*args, **kwargs):
    app(*args, **kwargs)
