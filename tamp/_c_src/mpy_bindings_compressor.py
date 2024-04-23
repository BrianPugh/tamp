import builtins


class Compressor:
    def __init__(
        self,
        f,
        *,
        window=10,
        literal=8,
        dictionary=None,
    ):
        self._cf = False  # shorter name to save binary space
        if not hasattr(f, "write"):  # It's probably a path-like object.
            f = builtins.open(str(f), "wb")
            self._cf = True
        self.f = f
        custom = dictionary is not None
        if not dictionary:
            dictionary = bytearray(1 << window)
        self._c = _C(f, window, literal, dictionary, custom)

    def write(self, data):
        return self._c.write(data)

    def close(self) -> int:
        bytes_written = self.flush(write_token=False)
        if self._cf:
            self.f.close()
        return bytes_written

    def flush(self, write_token=True):
        return self._c.flush(write_token)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()


TextCompressor = Compressor


def compress(data, *args, **kwargs) -> bytes:
    from io import BytesIO

    with BytesIO() as f:
        if isinstance(data, str):
            c = TextCompressor(f, *args, **kwargs)
            c.write(data)
        else:
            c = Compressor(f, *args, **kwargs)
            c.write(data)
        c.flush(write_token=False)
        f.seek(0)
        return f.read()
