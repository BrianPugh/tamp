cimport ctamp
from cpython.exc cimport PyErr_CheckSignals
from cpython.mem cimport PyMem_Malloc, PyMem_Free
from libc.stddef cimport size_t
from io import BytesIO
from . import bit_size
from ._c_common cimport CHUNK_SIZE
from ._c_common import ERROR_LOOKUP


from typing import Union


cdef class Decompressor:
    cdef:
        ctamp.TampDecompressor* _c_decompressor
        bytearray _window_buffer
        unsigned char *_window_buffer_ptr
        object input_buffer
        unsigned char *input_buffer_ptr
        size_t input_size
        size_t input_consumed
        object f
        cdef bint _close_f_on_close

    def __cinit__(self):
        self._c_decompressor = <ctamp.TampDecompressor*>PyMem_Malloc(sizeof(ctamp.TampDecompressor))
        if self._c_decompressor is NULL:
            raise MemoryError

    def __dealloc__(self):
        PyMem_Free(self._c_decompressor)

    def __init__(self, f, *, dictionary=None):
        cdef:
            ctamp.TampConf conf
            size_t input_consumed_size;

        if not hasattr(f, "read"):  # It's probably a path-like object.
            f = open(str(f), "rb")
            self._close_f_on_close = True
        else:
            self._close_f_on_close = False

        self.f = f

        self.input_buffer = bytearray(CHUNK_SIZE)
        self.input_buffer_ptr = self.input_buffer
        self.input_size = 0
        self.input_consumed = 0

        compressed_data = f.read(1)

        res = ctamp.tamp_decompressor_read_header(&conf, compressed_data, len(compressed_data), &input_consumed_size);
        if res != ctamp.TAMP_OK:
            raise ERROR_LOOKUP.get(res, NotImplementedError)
        if conf.use_custom_dictionary and dictionary is None:
            raise ValueError

        self._window_buffer = dictionary if dictionary else bytearray(1 << conf.window)
        self._window_buffer_ptr = <unsigned char *>self._window_buffer

        res = ctamp.tamp_decompressor_init(self._c_decompressor, &conf, self._window_buffer_ptr)
        if res < 0:
            raise ERROR_LOOKUP.get(res, NotImplementedError)

    def read(self, int size = -1) -> bytearray:
        cdef:
            bytearray output_buffer = bytearray(CHUNK_SIZE)
            unsigned char *output_buffer_ptr = output_buffer

            size_t input_chunk_consumed
            size_t output_size
            size_t output_written_size

        if size < 0:
            size = 0x7FFFFFFF

        output_list = []

        while size:
            output_size = min(CHUNK_SIZE, size)

            res = ctamp.tamp_decompressor_decompress(
                self._c_decompressor,
                output_buffer_ptr,
                output_size,
                &output_written_size,
                self.input_buffer_ptr + self.input_consumed,
                self.input_size,
                &input_chunk_consumed
            )
            self.input_size -= input_chunk_consumed
            self.input_consumed += input_chunk_consumed
            size -= output_written_size

            output_list.append(output_buffer[:output_written_size])

            if res == ctamp.TAMP_INPUT_EXHAUSTED:
                # Read in more data
                self.input_size = self.f.readinto(self.input_buffer)
                self.input_consumed = 0
                if self.input_size == 0:
                    break;
            elif res < 0:
                raise ERROR_LOOKUP.get(res, NotImplementedError)

            # Check signals for things like KeyboardInterrupt
            PyErr_CheckSignals()

        return bytearray().join(output_list)

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
    with BytesIO(data) as f:
        d = Decompressor(f, *args, **kwargs)
        return d.read()
