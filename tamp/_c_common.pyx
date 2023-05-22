cimport ctamp
from . import ExcessBitsError

cdef int CHUNK_SIZE = (1 << 20)

ERROR_LOOKUP = {
    # Recoverable Errors
    ctamp.TAMP_OUTPUT_FULL: IndexError,
    ctamp.TAMP_INPUT_EXHAUSTED: IndexError,

    # Bad Errors
    ctamp.TAMP_ERROR: Exception,
    ctamp.TAMP_EXCESS_BITS: ExcessBitsError,
    ctamp.TAMP_INVALID_CONF: ValueError,
}
