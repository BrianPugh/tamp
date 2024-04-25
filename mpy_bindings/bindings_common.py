__version__ = "0.0.0"


class ExcessBitsError(Exception):
    """Provided data has more bits than expected ``literal`` bits."""


def open(f, mode="rb", **kwargs):
    if "r" in mode:  # Decompressor
        if "w" in mode:
            raise ValueError
        return (Decompressor if "b" in mode else TextDecompressor)(f, **kwargs)
    elif "w" in mode:  # Compressor
        return (Compressor if "b" in mode else TextCompressor)(f, **kwargs)
    else:
        raise ValueError
