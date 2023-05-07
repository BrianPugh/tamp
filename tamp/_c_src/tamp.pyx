cimport ctamp
from cpython.mem cimport PyMem_Malloc, PyMem_Free


cdef class Compressor:
    cdef ctamp.TampCompressor* _c_compressor
    cdef ctamp.TampConf* _c_conf
    cdef object f

    def __cinit__(self):
        self._c_conf = <ctamp.TampConf*>PyMem_Malloc(sizeof(ctamp.TampConf))
        if self._c_conf is NULL:
            raise MemoryError

        self._c_compressor = <ctamp.TampCompressor*>PyMem_Malloc(sizeof(ctamp.TampCompressor))
        if self._c_compressor is NULL:
            PyMem_Free(self._c_conf)
            raise MemoryError


    def __dealloc__(self):
        PyMem_Free(self._c_conf)
        PyMem_Free(self._c_compressor)

    def __init__(
        self,
        f,
        *,
        window=10,
        literal=8,
        dictionary=None,
    ):
        if not hasattr(f, "write"):  # It's probably a path-like object.
            f = open(str(f), "wb")

        self.f = f

        self._c_conf.window = window
        self._c_conf.literal = literal
        self._c_conf.use_custom_dictionary = bool(dictionary)
