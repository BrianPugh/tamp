from pathlib import Path
from typing import Literal, Optional, Union, overload

from .compressor import Compressor as Compressor
from .compressor import TextCompressor as TextCompressor
from .decompressor import Decompressor as Decompressor
from .decompressor import TextDecompressor as TextDecompressor

__version__: str

PathLike = Union[Path, str]

OpenTextModeWriting = Literal["w", "wt", "tw"]
OpenTextModeReading = Literal["r", "rt", "tr"]
OpenTextMode = Union[OpenTextModeWriting, OpenTextModeReading]

OpenBinaryModeWriting = Literal["wb", "bw"]
OpenBinaryModeReading = Literal["rb", "br"]
OpenBinaryMode = Union[OpenBinaryModeReading, OpenBinaryModeWriting]

OpenModeWriting = Union[OpenTextModeWriting, OpenBinaryModeWriting]
OpenModeReading = Union[OpenTextModeReading, OpenBinaryModeReading]

@overload
def open(
    f: PathLike,
    mode: OpenBinaryModeReading,
    **kwargs,
) -> Decompressor: ...
@overload
def open(
    f: PathLike,
    mode: OpenTextModeReading,
    **kwargs,
) -> TextDecompressor: ...
@overload
def open(
    f: PathLike,
    mode: OpenBinaryModeWriting,
    **kwargs,
) -> Compressor: ...
@overload
def open(
    f: PathLike,
    mode: OpenTextModeWriting,
    **kwargs,
) -> TextCompressor: ...
def bit_size(value: int) -> int: ...
def initialize_dictionary(size: int, seed: int | None = None) -> bytearray: ...
def compute_min_pattern_size(window: int, literal: int) -> int: ...
