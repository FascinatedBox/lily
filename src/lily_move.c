#include "lily_core_types.h"
#include "lily_value_structs.h"
#include "lily_value_flags.h"

#include "lily_api_value.h"

#define MOVE_PRIM(name, in_type, field, f) \
void lily_move_##name(lily_value *v, in_type z) \
{ \
    if (v->flags & VAL_IS_DEREFABLE) \
        lily_deref(v); \
\
    v->value.field = z; \
    v->flags = f; \
}

#define MOVE_FLAG(name, in_type, field, type_flag) \
void lily_move_##name(uint16_t f, lily_value *v) \
{ \
    if (v->flags & VAL_IS_DEREFABLE) \
        lily_deref(v); \
\
    v->value.integer = 0; \
    v->flags = (f | type_flag); \
}

#define MOVE_FN(name, in_type, field, f) \
void lily_move_##name(lily_value *v, in_type z) \
{ \
    if (v->flags & VAL_IS_DEREFABLE) \
        lily_deref(v); \
\
    z->refcount++; \
    v->value.field = z; \
    v->flags = f; \
}

#define MOVE_FN_F(name, in_type, field, type_flag) \
void lily_move_##name##_f(uint32_t f, lily_value *v, in_type z) \
{ \
    if (v->flags & VAL_IS_DEREFABLE) \
        lily_deref(v); \
\
    z->refcount++; \
    v->value.field = z; \
    v->flags = (f | type_flag); \
}

#define CAST_FN_F(name, in_type, field, type_flag, cast_type) \
void lily_move_##name##_f(uint32_t f, lily_value *v, in_type z) \
{ \
    if (v->flags & VAL_IS_DEREFABLE) \
        lily_deref(v); \
\
    z->refcount++; \
    v->value.field = (cast_type)z; \
    v->flags = (f | type_flag); \
}

MOVE_PRIM(boolean,        int64_t,             integer,   LILY_BOOLEAN_ID)
MOVE_PRIM(byte,           uint8_t,             integer,   LILY_BYTE_ID)
MOVE_FN  (bytestring,     lily_string_val *,   string,    LILY_BYTESTRING_ID | VAL_IS_DEREFABLE)
MOVE_PRIM(double,         double,              doubleval, LILY_DOUBLE_ID)
MOVE_FN  (dynamic,        lily_dynamic_val *,  dynamic,   LILY_DYNAMIC_ID    | VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE)
MOVE_FLAG(empty_variant,  lily_instance_val *, instance,  VAL_IS_ENUM)
MOVE_FN_F(foreign,        lily_foreign_val *,  foreign,   VAL_IS_FOREIGN     | VAL_IS_DEREFABLE)
MOVE_FN_F(instance,       lily_instance_val *, instance,  VAL_IS_INSTANCE)
MOVE_FN  (file,           lily_file_val *,     file,      LILY_FILE_ID       | VAL_IS_DEREFABLE)
MOVE_FN_F(function,       lily_function_val *, function,  LILY_FUNCTION_ID)
MOVE_FN_F(hash,           lily_hash_val *,     hash,      LILY_HASH_ID)
MOVE_PRIM(integer,        int64_t,             integer,   LILY_INTEGER_ID)
MOVE_FN_F(list,           lily_list_val *,     list,      LILY_LIST_ID)
MOVE_FN  (string,         lily_string_val *,   string,    LILY_STRING_ID     | VAL_IS_DEREFABLE)
CAST_FN_F(tuple,          lily_tuple_val *,    list,      LILY_TUPLE_ID, lily_list_val *)
MOVE_PRIM(unit,           int64_t,             integer,   LILY_UNIT_ID)
CAST_FN_F(variant,        lily_variant_val *,  instance,  VAL_IS_ENUM, lily_instance_val *)
