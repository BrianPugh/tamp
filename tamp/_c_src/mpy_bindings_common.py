__version__ = "0.0.0"


class ExcessBitsError(Exception):
    """Provided data has more bits than expected ``literal`` bits."""


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
