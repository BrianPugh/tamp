/**
 * WASM-only sizeof functions
 *
 * These functions expose struct sizes to JavaScript so it can allocate
 * the correct amount of memory without hardcoding sizes that may drift.
 *
 * This file is ONLY compiled for the WASM target - it is not part of
 * the core C library used by embedded targets.
 */

#include "../../tamp/_c_src/tamp/common.h"
#include "../../tamp/_c_src/tamp/compressor.h"
#include "../../tamp/_c_src/tamp/decompressor.h"

// Round up to 4-byte alignment for safe access from JavaScript
#define ALIGN4(x) (((x) + 3) & ~(size_t)3)

size_t tamp_compressor_sizeof(void) { return ALIGN4(sizeof(TampCompressor)); }

size_t tamp_decompressor_sizeof(void) { return ALIGN4(sizeof(TampDecompressor)); }

size_t tamp_conf_sizeof(void) { return ALIGN4(sizeof(TampConf)); }
