__version__ = "0.0.0"


from tamp.decompressor import TextDecompressor


class ExcessBitsError(Exception):
    """Provided data has more bits than expected ``literal`` bits."""


def open(f, mode="rb", **kwargs):
    if "r" in mode and "w" in mode:
        raise ValueError

    if "r" in mode:  # Decompressor
        return Decompressor(f, **kwargs) if "b" in mode else TextDecompressor(f, **kwargs)
    elif "w" in mode:  # Compressor
        return Compressor(f, **kwargs) if "b" in mode else TextCompressor(f, **kwargs)
    else:
        raise ValueError
