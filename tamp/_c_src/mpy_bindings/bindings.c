#include "py/dynruntime.h"
#include "tamp/compressor.h"
#include "tamp/decompressor.h"

// This is the entry point and is called when the module is imported
mp_obj_t mpy_init(mp_obj_fun_bc_t *self, size_t n_args, size_t n_kw, mp_obj_t *args) {
    // This must be first, it sets up the globals dict and other things
    MP_DYNRUNTIME_INIT_ENTRY

    // This must be last, it restores the globals dict
    MP_DYNRUNTIME_INIT_EXIT
}
