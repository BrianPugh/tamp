class Decompressor:
    def __init__(
        self,
        f,
        *,
        dictionary=None,
    ):
        self._cf = False  # shorter name to save binary space
        if not hasattr(f, "read"):  # It's probably a path-like object.
            f = open(str(f), "rb")
            self._cf = True
        self.f = f
        # dictionary is checked further in C
        self._d = _D(f, dictionary)

    def read(self, size=-1):  # -> bytearray
        chunks = []
        chunk_size = 256  # noqa: N806
        while True:
            chunk = bytearray(chunk_size)
            bytes_read = self.readinto(chunk)
            if not bytes_read:
                break
            if bytes_read < chunk_size:
                chunk = memoryview(chunk)[:bytes_read]
            if chunk_size < 32768:  # sets an upper bound on allocation.
                chunk_size <<= 1
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
        if self._cf:
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
