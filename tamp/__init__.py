# Don't manually change, let poetry-dynamic-versioning-plugin handle it.
__version__ = "0.0.0"


class ExcessBitsError(Exception):
    """Provided data has more bits than expected ``literal`` bits."""


def bit_size(value):
    for i in range(32):
        if not value:
            return i
        value >>= 1
    return -1


def _xorshift32(seed):
    while True:
        seed ^= (seed << 13) & 0xFFFFFFFF
        seed ^= (seed >> 17) & 0xFFFFFFFF
        seed ^= (seed << 5) & 0xFFFFFFFF
        yield seed


def initialize_dictionary(size, seed=None):
    if seed is None:
        seed = 3758097560
    elif seed == 0:
        return bytearray(size)

    chars = b" \x000ei>to<ans\nr/."  # 16 most common chars in dataset

    def _gen_stream(xorshift32):
        for _ in range(size >> 3):
            value = next(xorshift32)
            yield chars[value & 0x0F]
            yield chars[value >> 4 & 0x0F]
            yield chars[value >> 8 & 0x0F]
            yield chars[value >> 12 & 0x0F]
            yield chars[value >> 16 & 0x0F]
            yield chars[value >> 20 & 0x0F]
            yield chars[value >> 24 & 0x0F]
            yield chars[value >> 28 & 0x0F]

    return bytearray(_gen_stream(_xorshift32(seed)))


def compute_min_pattern_size(window, literal):
    """Compute whether the minimum pattern length should be 2 or 3."""
    if window > 15 or window < 8:
        raise ValueError
    if literal == 5:
        return 2 + (window > 10)
    elif literal == 6:
        return 2 + (window > 12)
    elif literal == 7:
        return 2 + (window > 14)
    elif literal == 8:
        return 2
    else:
        raise ValueError


try:
    from .compressor_viper import Compressor, TextCompressor, compress
except ImportError:
    try:
        from ._c_compressor import Compressor, TextCompressor, compress
    except ImportError:
        try:
            from .compressor import Compressor, TextCompressor, compress
        except ImportError:
            pass

try:
    from .decompressor_viper import Decompressor, TextDecompressor, decompress
except ImportError:
    try:
        from ._c_decompressor import Decompressor, TextDecompressor, decompress
    except ImportError:
        try:
            from .decompressor import Decompressor, TextDecompressor, decompress
        except ImportError:
            pass


def open(f, mode="rb", **kwargs):
    if "r" in mode and "w" in mode:
        raise ValueError

    if "r" in mode:  # Decompressor
        if "b" in mode:
            return Decompressor(f, **kwargs)
        else:
            return TextDecompressor(f, **kwargs)
    elif "w" in mode:  # Compressor
        if "b" in mode:
            return Compressor(f, **kwargs)
        else:
            return TextCompressor(f, **kwargs)
    else:
        raise ValueError
