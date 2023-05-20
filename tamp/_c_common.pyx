cimport ctamp
from . import ExcessBitsError

cdef int CHUNK_SIZE = (1 << 20)

ERROR_LOOKUP = {
    ctamp.TAMP_EXCESS_BITS: ExcessBitsError,
}
