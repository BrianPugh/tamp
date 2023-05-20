cimport ctamp
from cpython.mem cimport PyMem_Malloc, PyMem_Free
from libc.stddef cimport size_t
from io import BytesIO
from . import bit_size
from ._c_common cimport CHUNK_SIZE
from ._c_common import ERROR_LOOKUP


from typing import Union


cdef class Decompressor:
    cdef ctamp.TampDecompressor* _c_decompressor
    cdef bytearray _window_buffer
    cdef unsigned char *_window_buffer_ptr
    cdef object f

    def __cinit__(self):
        self._c_decompressor = <ctamp.TampDecompressor*>PyMem_Malloc(sizeof(ctamp.TampDecompressor))
        if self._c_decompressor is NULL:
            raise MemoryError

    def __dealloc__(self):
        PyMem_Free(self._c_decompressor)

    def __init__(self, f, *, dictionary=None):
        if not hasattr(f, "write"):  # It's probably a path-like object.
            f = open(str(f), "wb")

        self.f = f

        raise NotImplementedError

    def read(self, size=-1) -> bytearray:
        raise NotImplementedError

    def close(self):
        raise NotImplementedError

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()

class TextDecompressor(Decompressor):
    """Decompresses a file or stream of tamp-compressed data into text."""

    def read(self, *args, **kwargs) -> str:
        return super().read(*args, **kwargs).decode()


def decompress(data: bytes, *args, **kwargs) -> bytearray:
    with BytesIO(data) as f:
        d = Decompressor(f, *args, **kwargs)
        return d.read()
