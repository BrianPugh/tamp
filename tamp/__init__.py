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


def initialize_dictionary(source, seed=None, literal=8):
    if not (5 <= literal <= 8):
        raise ValueError("literal must be between 5 and 8")
    if seed is None:
        seed = 3758097560
    elif seed == 0:
        return bytearray(source)

    out = source if isinstance(source, bytearray) else bytearray(source)
    size = len(out)

    # Common characters for dictionary seeding.
    # ' etaoinshrdlcumw' downshifted to the appropriate bit width.
    if literal <= 5:
        chars = bytes([c & 0x1F for c in b" etaoinshrdlcumw"])
    elif literal <= 6:
        chars = bytes([c & 0x3F for c in b" etaoinshrdlcumw"])
    else:
        chars = b" \x000ei>to<ans\nr/."

    def _gen_stream(xorshift32):
        for _ in range(size >> 3):
            value = next(xorshift32)
            for _ in range(8):
                yield chars[value & 0x0F]
                value >>= 4

    for i, c in enumerate(_gen_stream(_xorshift32(seed))):
        out[i] = c

    return out


def compute_min_pattern_size(window, literal):
    if not (7 < window < 16 and 4 < literal < 9):
        raise ValueError

    return 2 + (window > (10 + ((literal - 5) << 1)))


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
        return Decompressor(f, **kwargs) if "b" in mode else TextDecompressor(f, **kwargs)
    elif "w" in mode:  # Compressor
        return Compressor(f, **kwargs) if "b" in mode else TextCompressor(f, **kwargs)
    else:
        raise ValueError
