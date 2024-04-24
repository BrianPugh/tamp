/**
 * Micropython Native Module Bindings.
 * Requires Micropython >= 1.21.0
 */
#include "py/dynruntime.h"

/**********
 * COMMON *
 **********/

#include "tamp/common.h"
#define CHUNK_SIZE 16  // Must be <= 65535
#define mp_type_bytearray (*(mp_obj_type_t *)(mp_load_global(MP_QSTR_bytearray)))

static void TAMP_CHECK(tamp_res res){
    if(res == TAMP_EXCESS_BITS){
        nlr_raise(mp_obj_new_exception(mp_load_global(MP_QSTR_ExcessBitsError)));
    }
    else if(res < TAMP_OK){
        mp_raise_ValueError("");
    }
}

static mp_obj_t initialize_dictionary(mp_obj_t size_obj) {
    mp_int_t size = mp_obj_get_int(size_obj);
    uint8_t *buffer = m_malloc(size);
    mp_obj_t buffer_obj = mp_obj_new_bytearray_by_ref(size, buffer);
    tamp_initialize_dictionary(buffer, size);
    return buffer_obj;
}
MP_DEFINE_CONST_FUN_OBJ_1(initialize_dictionary_obj, initialize_dictionary);

/**************
 * COMPRESSOR *
 **************/
#if TAMP_COMPRESSOR
#include "tamp/compressor.h"

typedef struct {
    mp_obj_base_t base;
    mp_obj_t dictionary;  // To prevent GC from collecting python-provided window_buffer.
    mp_obj_t f;
    TampCompressor c;
} mp_obj_compressor_t;

mp_obj_full_type_t mp_type_compressor;  // This is type(Compressor)
mp_map_elem_t compressor_locals_dict_table[2];  // Number of Compressor methods/attributes
static MP_DEFINE_CONST_DICT(compressor_locals_dict, compressor_locals_dict_table);


// Essentially Compressor.__new__ (but also kind of __init__).
static mp_obj_t compressor_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args_in) {
    TampConf conf = {
        .window=mp_obj_get_int(args_in[1]),
        .literal=mp_obj_get_int(args_in[2]),
        .use_custom_dictionary=mp_obj_get_int(args_in[4]),
    };

    mp_obj_compressor_t *o = mp_obj_malloc(mp_obj_compressor_t, type);
    o->f = args_in[0];
    o->dictionary = args_in[3];

    mp_buffer_info_t dictionary_buffer_info;
    mp_get_buffer_raise(o->dictionary, &dictionary_buffer_info, MP_BUFFER_RW);
    if(dictionary_buffer_info.len < (1 << conf.window)){
        mp_raise_ValueError("");
    }

    TAMP_CHECK(tamp_compressor_init(&o->c, &conf, dictionary_buffer_info.buf));

    return MP_OBJ_FROM_PTR(o);
}

static mp_obj_t compressor_write(mp_obj_t self_in, mp_obj_t data_in){
    mp_obj_compressor_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t input_buffer_info;

    mp_get_buffer_raise(data_in, &input_buffer_info, MP_BUFFER_READ);

    size_t written_to_disk_size = 0;
    uint8_t *input_buffer_end = (uint8_t*)input_buffer_info.buf + input_buffer_info.len;
    size_t output_buffer_written_size, input_consumed_size;
    uint8_t output_buffer[CHUNK_SIZE];

    mp_obj_t write_method = mp_load_attr(self->f, MP_QSTR_write);
    mp_obj_t bytearray_obj = mp_obj_new_bytearray_by_ref(CHUNK_SIZE, output_buffer);
    mp_obj_array_t *bytearray_ptr = MP_OBJ_TO_PTR(bytearray_obj);

    for(
            uint8_t *input_buffer = (uint8_t*)input_buffer_info.buf;
            input_buffer < input_buffer_end;
            input_buffer += input_consumed_size
        ){
        TAMP_CHECK(tamp_compressor_compress(
            &self->c,
            output_buffer,
            CHUNK_SIZE,
            &output_buffer_written_size,
            input_buffer,
            input_buffer_end - input_buffer,
            &input_consumed_size
        ));
        bytearray_ptr->len = output_buffer_written_size;
        mp_call_function_n_kw(write_method, 1, 0, &bytearray_obj);
        written_to_disk_size += output_buffer_written_size;
    }

    return mp_obj_new_int(written_to_disk_size);
}
static MP_DEFINE_CONST_FUN_OBJ_2(compressor_write_obj, compressor_write);

static mp_obj_t compressor_flush(mp_obj_t self_in, mp_obj_t write_token_in){
    mp_obj_compressor_t *self = MP_OBJ_TO_PTR(self_in);
    uint8_t output_buffer[CHUNK_SIZE];
    size_t output_written_size;

    TAMP_CHECK(tamp_compressor_flush(
        &self->c,
        output_buffer,
        CHUNK_SIZE,
        &output_written_size,
        mp_obj_get_int(write_token_in)
    ));
    mp_obj_t bytes_obj = mp_obj_new_bytes(output_buffer, output_written_size);
    mp_obj_t write_method = mp_load_attr(self->f, MP_QSTR_write);
    mp_call_function_n_kw(write_method, 1, 0, &bytes_obj);

    return mp_obj_new_int(output_written_size);
}
static MP_DEFINE_CONST_FUN_OBJ_2(compressor_flush_obj, compressor_flush);
#endif

/****************
 * DECOMPRESSOR *
 ****************/
#if TAMP_DECOMPRESSOR
#include "tamp/decompressor.h"

typedef struct {
    mp_obj_base_t base;
    mp_obj_t dictionary;  // To prevent GC from collecting user-provided window_buffer.
    mp_obj_t f;
    TampDecompressor d;
    mp_obj_t input_buffer_obj;
    uint16_t input_buffer_size;
    uint16_t input_buffer_consumed_size;
} mp_obj_decompressor_t;

mp_obj_full_type_t mp_type_decompressor;  // This is type(decompressor)
mp_map_elem_t decompressor_locals_dict_table[1];  // Number of decompressor methods/attributes
static MP_DEFINE_CONST_DICT(decompressor_locals_dict, decompressor_locals_dict_table);


// Essentially Decompressor.__new__ (but also kind of __init__).
static mp_obj_t decompressor_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args_in) {
    TampConf conf;

    mp_obj_decompressor_t *o = mp_obj_malloc(mp_obj_decompressor_t, type);
    o->f = args_in[0];
    o->dictionary = args_in[1];
    o->input_buffer_obj = mp_obj_new_bytearray_by_ref(CHUNK_SIZE, m_malloc(CHUNK_SIZE));

    /* Initial population of input_buffer_obj */
    mp_obj_t readinto_method = mp_load_attr(o->f, MP_QSTR_readinto);
    mp_obj_t amount_read = mp_call_function_n_kw(readinto_method, 1, 0, &o->input_buffer_obj);
    o->input_buffer_size = mp_obj_get_int(amount_read);
    o->input_buffer_consumed_size = 0;

    {
        /* Read the Tamp Header */
        size_t input_buffer_consumed_size;
        mp_buffer_info_t input_buffer_info;
        mp_get_buffer_raise(o->input_buffer_obj, &input_buffer_info, MP_BUFFER_READ);
        TAMP_CHECK(tamp_decompressor_read_header(
                &conf,
                input_buffer_info.buf,
                input_buffer_info.len,
                &input_buffer_consumed_size
                ));
        o->input_buffer_consumed_size += input_buffer_consumed_size;
    }

    const uint16_t window_size = 1 << conf.window;
    if(o->dictionary == mp_const_none){
        if(conf.use_custom_dictionary){
            mp_raise_ValueError("");
        }
        o->dictionary = mp_obj_new_bytearray_by_ref(window_size, m_malloc(window_size));
    }

    {
        mp_buffer_info_t dictionary_buffer_info;
        mp_get_buffer_raise(o->dictionary, &dictionary_buffer_info, MP_BUFFER_RW);
        if (dictionary_buffer_info.len < window_size){
            mp_raise_ValueError("");
        }

        TAMP_CHECK(tamp_decompressor_init(&o->d, &conf, dictionary_buffer_info.buf));
    }

    return MP_OBJ_FROM_PTR(o);
}

static mp_obj_t decompressor_readinto(mp_obj_t self_in_obj, mp_obj_t output_buffer_obj){
    mp_obj_decompressor_t *self = MP_OBJ_TO_PTR(self_in_obj);
    tamp_res res;
    mp_buffer_info_t output_buffer_info, input_buffer_info;
    size_t total_written_size = 0;

    mp_obj_t readinto_method = mp_load_attr(self->f, MP_QSTR_readinto);
    mp_get_buffer_raise(output_buffer_obj, &output_buffer_info, MP_BUFFER_WRITE);
    mp_get_buffer_raise(self->input_buffer_obj, &input_buffer_info, MP_BUFFER_RW);

    do{
        size_t output_buffer_written_size, input_buffer_consumed_size;
        res = tamp_decompressor_decompress(
            &self->d,
            output_buffer_info.buf + total_written_size,
            output_buffer_info.len - total_written_size,
            &output_buffer_written_size,
            input_buffer_info.buf + self->input_buffer_consumed_size,
            self->input_buffer_size - self->input_buffer_consumed_size,
            &input_buffer_consumed_size
        );
        total_written_size += output_buffer_written_size;
        self->input_buffer_consumed_size += input_buffer_consumed_size;

        if (res == TAMP_INPUT_EXHAUSTED){
            mp_obj_t bytes_read = mp_call_function_n_kw(readinto_method, 1, 0, &self->input_buffer_obj);
            self->input_buffer_size = mp_obj_get_int(bytes_read);
            self->input_buffer_consumed_size = 0;
            if(self->input_buffer_size == 0){
                break;
            }
        }
        TAMP_CHECK(res);
    } while(res != TAMP_OUTPUT_FULL);

    return mp_obj_new_int(total_written_size);
}
static MP_DEFINE_CONST_FUN_OBJ_2(decompressor_readinto_obj, decompressor_readinto);
#endif

/***************
 * MODULE INIT *
 ***************/

// This is the entry point and is called when the module is imported
mp_obj_t mpy_init(mp_obj_fun_bc_t *self, size_t n_args, size_t n_kw, mp_obj_t *args) {
    // This must be first, it sets up the globals dict and other things
    MP_DYNRUNTIME_INIT_ENTRY

    /**********
     * COMMON *
     **********/
    mp_store_global(MP_QSTR_initialize_dictionary, MP_OBJ_FROM_PTR(&initialize_dictionary_obj));

    /**************
     * COMPRESSOR *
     **************/
#if TAMP_COMPRESSOR
    // Initialise the type.
    mp_type_compressor.base.type = (void*)&mp_type_type;
    mp_type_compressor.flags = MP_TYPE_FLAG_NONE;
    mp_type_compressor.name = MP_QSTR__C;

    // Set the constructor
    MP_OBJ_TYPE_SET_SLOT(&mp_type_compressor, make_new, compressor_make_new, 0);
    MP_OBJ_TYPE_SET_SLOT(&mp_type_compressor, locals_dict, (void*)&compressor_locals_dict, 1);

    // Add methods
    compressor_locals_dict_table[0] = (mp_map_elem_t){ MP_OBJ_NEW_QSTR(MP_QSTR_write), MP_OBJ_FROM_PTR(&compressor_write_obj) };
    compressor_locals_dict_table[1] = (mp_map_elem_t){ MP_OBJ_NEW_QSTR(MP_QSTR_flush), MP_OBJ_FROM_PTR(&compressor_flush_obj) };

    // Make the _Compressor type available on the module.
    mp_store_global(MP_QSTR__C, MP_OBJ_FROM_PTR(&mp_type_compressor));
#endif
    /****************
     * DECOMPRESSOR *
     ****************/
#if TAMP_DECOMPRESSOR
    mp_type_decompressor.base.type = (void*)&mp_type_type;
    mp_type_decompressor.flags = MP_TYPE_FLAG_NONE;
    mp_type_decompressor.name = MP_QSTR__D;

    // Set the constructor
    MP_OBJ_TYPE_SET_SLOT(&mp_type_decompressor, make_new, decompressor_make_new, 0);
    MP_OBJ_TYPE_SET_SLOT(&mp_type_decompressor, locals_dict, (void*)&decompressor_locals_dict, 1);

    // Add methods
    decompressor_locals_dict_table[0] = (mp_map_elem_t){ MP_OBJ_NEW_QSTR(MP_QSTR_readinto), MP_OBJ_FROM_PTR(&decompressor_readinto_obj) };

    // Make the _Decompressor type available on the module.
    mp_store_global(MP_QSTR__D, MP_OBJ_FROM_PTR(&mp_type_decompressor));
#endif

    /***********
     * CLEANUP *
     ***********/
    // This must be last, it restores the globals dict
    MP_DYNRUNTIME_INIT_EXIT
}
