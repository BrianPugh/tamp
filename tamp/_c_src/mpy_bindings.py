class Compressor:
    def __init__(
        self,
        f,
        *,
        window=10,
        literal=8,
        dictionary=None,
    ):
        self._close_f_on_close = False
        if not hasattr(f, "write"):  # It's probably a path-like object.
            f = open(str(f), "wb")
            self._close_f_on_close = True
        self.f = f
        custom = dictionary is not None
        if not dictionary:
            dictionary = bytearray(1 << window)
        self._c = _Compressor(f, window, literal, dictionary, custom)

    def write(self, data):
        return self._c.write(data)

    def close(self) -> int:
        bytes_written = self.flush(write_token=False)
        if self._close_f_on_close:
            self.f.close()
        return bytes_written

    def flush(self, write_token=True):
        return self._c.flush(write_token)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()


class TextCompressor(Compressor):
    """Compresses text to a file or stream."""

    def write(self, data: str) -> int:
        return super().write(data.encode())


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


class Decompressor:
    def __init__(
        self,
        f,
        *,
        dictionary=None,
    ):
        self._close_f_on_close = False
        if not hasattr(f, "read"):  # It's probably a path-like object.
            f = open(str(f), "rb")
            self._close_f_on_close = True
        self.f = f
        # dictionary is checked further in C
        self._d = _Decompressor(f, dictionary)

    def read(self, size=-1):  # -> bytearray
        chunks = []
        CHUNK_SIZE = 256  # noqa: N806
        while True:
            chunk = bytearray(CHUNK_SIZE)
            bytes_read = self.readinto(chunk)
            if not bytes_read:
                break
            if bytes_read < CHUNK_SIZE:
                chunk = memoryview(chunk)[:bytes_read]
            chunks.append(chunk)

        out = bytearray(sum(len(x) for x in chunks))
        start_idx = 0
        for chunk in chunks:
            end_idx = start_idx + len(chunk)
            out[start_idx:end_idx] = chunk
            start_idx = end_idx

        return out

    def readinto(self, buf):  # -> int
        return self._d.readinto(buf)

    def close(self):
        if self._close_f_on_close:
            self.f.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()


class TextDecompressor(Decompressor):
    """Decompresses a file or stream of tamp-compressed data into text."""

    def read(self, *args, **kwargs) -> str:
        return super().read(*args, **kwargs).decode()


def decompress(data: bytes, *args, **kwargs) -> bytearray:
    from io import BytesIO

    with BytesIO(data) as f:
        d = Decompressor(f, *args, **kwargs)
        return d.read()
