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
    """Initialize Dictionary.

    Parameters
    ----------
    size: Union[int, bytearray]
        If a ``bytearray``, will populate it with initial data.
        If an ``int``, will allocate and initialize a bytearray of indicated size.

    Returns
    -------
    bytearray
        Initialized window dictionary.
    """
    if seed is None:
        seed = 3758097560
    elif seed == 0:
        return bytearray(size)

    if isinstance(size, bytearray):
        out = size
        size = len(out)
    else:
        out = bytearray(size)

    chars = b" \x000ei>to<ans\nr/."  # 16 most common chars in dataset

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
    """Compute whether the minimum pattern length should be 2 or 3.

    .. code-block:: python

        # Easy to understand version; commented out for smaller optimized version;
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
    """
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
