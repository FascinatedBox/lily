#include "lily_value_structs.h"

#include "lily_api_value_flags.h"
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

MOVE_PRIM(boolean,        int64_t,             integer,   VAL_IS_BOOLEAN)
MOVE_PRIM(double,         double,              doubleval, VAL_IS_DOUBLE)
MOVE_FN  (dynamic,        lily_dynamic_val *,  dynamic,   VAL_IS_DYNAMIC  | VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE)
MOVE_PRIM(empty_variant,  lily_instance_val *, instance,  VAL_IS_INSTANCE)
MOVE_FN_F(enum,           lily_instance_val *, instance,  VAL_IS_ENUM)
MOVE_FN  (file,           lily_file_val *,     file,      VAL_IS_FILE     | VAL_IS_DEREFABLE)
MOVE_FN_F(foreign,        lily_foreign_val *,  foreign,   VAL_IS_FOREIGN  | VAL_IS_DEREFABLE)
MOVE_FN_F(function,       lily_function_val *, function,  VAL_IS_FUNCTION)
MOVE_FN_F(hash,           lily_hash_val *,     hash,      VAL_IS_HASH)
MOVE_PRIM(integer,        int64_t,             integer,   VAL_IS_INTEGER)
MOVE_FN_F(instance,       lily_instance_val *, instance,  VAL_IS_INSTANCE)
MOVE_FN_F(list,           lily_list_val *,     list,      VAL_IS_LIST)
MOVE_FN  (string,         lily_string_val *,   string,    VAL_IS_STRING   | VAL_IS_DEREFABLE)
MOVE_FN_F(tuple,          lily_list_val *,     list,      VAL_IS_TUPLE)
