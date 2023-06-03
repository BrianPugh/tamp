cimport ctamp
from cpython.exc cimport PyErr_CheckSignals
from cpython.mem cimport PyMem_Malloc, PyMem_Free
from libc.stddef cimport size_t
from io import BytesIO
from . import bit_size
from ._c_common cimport CHUNK_SIZE
from ._c_common import ERROR_LOOKUP

from typing import Union


cdef class Compressor:
    cdef ctamp.TampCompressor* _c_compressor
    cdef bytearray _window_buffer
    cdef unsigned char *_window_buffer_ptr
    cdef object f
    cdef bint _close_f_on_close

    def __cinit__(self):
        self._c_compressor = <ctamp.TampCompressor*>PyMem_Malloc(sizeof(ctamp.TampCompressor))
        if self._c_compressor is NULL:
            raise MemoryError

    def __dealloc__(self):
        PyMem_Free(self._c_compressor)

    def __init__(
        self,
        f,
        *,
        int window=10,
        int literal=8,
        dictionary=None,
    ):
        cdef ctamp.TampConf conf

        if dictionary and bit_size(len(dictionary) - 1) != window:
            raise ValueError("Dictionary-window size mismatch.")

        if not hasattr(f, "write"):  # It's probably a path-like object.
            f = open(str(f), "wb")
            self._close_f_on_close = True
        else:
            self._close_f_on_close = False

        self.f = f

        conf.window = window
        conf.literal = literal
        conf.use_custom_dictionary = bool(dictionary)

        self._window_buffer = dictionary if dictionary else bytearray(1 << window)
        self._window_buffer_ptr = <unsigned char *>self._window_buffer

        res = ctamp.tamp_compressor_init(self._c_compressor, &conf, self._window_buffer_ptr)
        if res < 0:
            raise ERROR_LOOKUP.get(res, NotImplementedError)

    def write(self, const unsigned char[::1] data not None) -> int:
        cdef:
            ctamp.tamp_res res

            bytearray output_buffer = bytearray(CHUNK_SIZE)
            unsigned char *output_buffer_ptr = output_buffer
            const unsigned char* data_ptr = &data[0]

            size_t input_consumed_size = 0
            size_t input_remaining_size = data.shape[0]
            size_t output_buffer_written_size
            int written_to_disk_size = 0

        output_buffer_mv = memoryview(output_buffer)

        while input_remaining_size:
            res = ctamp.tamp_compressor_compress(
                self._c_compressor,
                output_buffer_ptr,
                CHUNK_SIZE,
                &output_buffer_written_size,
                data_ptr,
                input_remaining_size,
                &input_consumed_size
            )
            if res < 0:
                raise ERROR_LOOKUP.get(res, NotImplementedError)
            self.f.write(output_buffer_mv[:output_buffer_written_size])
            written_to_disk_size += output_buffer_written_size
            data_ptr += input_consumed_size
            input_remaining_size -= input_consumed_size

            # Check signals for things like KeyboardInterrupt
            PyErr_CheckSignals()

        return written_to_disk_size

    cpdef int flush(self, write_token: bool = True) except -1:
        cdef ctamp.tamp_res res
        cdef bytearray buffer = bytearray(24)
        cdef size_t output_written_size = 0

        res = ctamp.tamp_compressor_flush(
            self._c_compressor,
            <unsigned char *> buffer,
            len(buffer),
            &output_written_size,
            write_token,
        )

        if res < 0:
            raise ERROR_LOOKUP.get(res, NotImplementedError)

        if output_written_size:
            self.f.write(buffer[:output_written_size])

        self.f.flush()

        return output_written_size

    def close(self) -> int:
        bytes_written = self.flush(write_token=False)
        if self._close_f_on_close:
            self.f.close()
        return bytes_written

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()


class TextCompressor(Compressor):
    """Compresses text to a file or stream."""

    def write(self, data: str) -> int:
        return super().write(data.encode())


def compress(data: Union[bytes, str], *args, **kwargs) -> bytes:
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
