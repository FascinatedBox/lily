#include <string.h>

#include "lily_alloc.h"
#include "lily_vm.h"
#include "lily_value.h"
#include "lily_core_types.h"

/* This is for their destroy funcs. :( */

#include "lily_cls_string.h"
#include "lily_cls_list.h"
#include "lily_cls_hash.h"
#include "lily_cls_dynamic.h"
#include "lily_cls_function.h"
#include "lily_cls_file.h"

void lily_destroy_value(lily_value *v)
{
    int flags = v->flags;
    if (flags & (VAL_IS_LIST | VAL_IS_INSTANCE | VAL_IS_TUPLE | VAL_IS_ENUM))
        lily_destroy_list(v);
    else if (flags & (VAL_IS_STRING | VAL_IS_BYTESTRING))
        lily_destroy_string(v);
    else if (flags & VAL_IS_FUNCTION)
        lily_destroy_function(v);
    else if (flags & VAL_IS_HASH)
        lily_destroy_hash(v);
    else if (flags & VAL_IS_DYNAMIC)
        lily_destroy_dynamic(v);
    else if (flags & VAL_IS_FILE)
        lily_destroy_file(v);
    else if (flags & VAL_IS_FOREIGN)
        v->value.foreign->destroy_func(v);
}

/* Check if the value given is deref-able. If so, hit it with a deref. */
void lily_deref(lily_value *value)
{
    if (value->flags & VAL_IS_DEREFABLE) {
        value->value.generic->refcount--;
        if (value->value.generic->refcount == 0)
            lily_destroy_value(value);
    }
}

lily_value *lily_new_value(uint64_t flags, lily_raw_value raw)
{
    lily_value *v = lily_malloc(sizeof(lily_value));
    v->flags = flags;
    v->value = raw;

    return v;
}

/* Assign one value to another. The right may get a ref, and the left may get a
   deref. Both sides are assumed to be equivalent type-wise (only value and
   flags move over). */
void lily_assign_value(lily_value *left, lily_value *right)
{
    if (right->flags & VAL_IS_DEREFABLE)
        right->value.generic->refcount++;

    if (left->flags & VAL_IS_DEREFABLE)
        lily_deref(left);

    left->value = right->value;
    left->flags = right->flags;
}

void lily_move(lily_value *v, lily_raw_value raw, int flags)
{
    if (v->flags & VAL_IS_DEREFABLE)
        lily_deref(v);

    v->value = raw;
    v->flags = flags;
}

#define MOVE_FN(name, in_type, field, f) \
void lily_move_##name(lily_value *v, in_type z) \
{ \
    if (v->flags & VAL_IS_DEREFABLE) \
        lily_deref(v); \
\
    v->value.field = z; \
    v->flags = f; \
}

MOVE_FN(boolean,        int64_t,             integer,   VAL_IS_BOOLEAN)
MOVE_FN(closure,        lily_function_val *, function,  VAL_IS_FUNCTION | VAL_IS_DEREFABLE | VAL_IS_GC_TAGGED)
MOVE_FN(double,         double,              doubleval, VAL_IS_DOUBLE)
MOVE_FN(dynamic,        lily_dynamic_val *,  dynamic,   VAL_IS_DYNAMIC  | VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE)
MOVE_FN(enum,           lily_instance_val *, instance,  VAL_IS_ENUM     | VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE)
MOVE_FN(file,           lily_file_val *,     file,      VAL_IS_FILE     | VAL_IS_DEREFABLE)
MOVE_FN(foreign,        lily_foreign_val *,  foreign,   VAL_IS_FOREIGN  | VAL_IS_DEREFABLE)
MOVE_FN(function,       lily_function_val *, function,  VAL_IS_FUNCTION | VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE)
MOVE_FN(hash,           lily_hash_val *,     hash,      VAL_IS_HASH     | VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE)
MOVE_FN(integer,        int64_t,             integer,   VAL_IS_INTEGER)
MOVE_FN(instance,       lily_instance_val *, instance,  VAL_IS_INSTANCE | VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE)
MOVE_FN(list,           lily_list_val *,     list,      VAL_IS_LIST     | VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE)
MOVE_FN(shared_enum,    lily_instance_val *, instance,  VAL_IS_ENUM                        | VAL_IS_GC_SPECULATIVE)
MOVE_FN(string,         lily_string_val *,   string,    VAL_IS_STRING   | VAL_IS_DEREFABLE)

/* Create a copy of a value. It may get a ref. */
lily_value *lily_copy_value(lily_value *input)
{
    if (input->flags & VAL_IS_DEREFABLE)
        input->value.generic->refcount++;

    return lily_new_value(input->flags, input->value);
}

static lily_string_val *new_sv(char *buffer, int size)
{
    lily_string_val *sv = lily_malloc(sizeof(lily_string_val));
    sv->refcount = 1;
    sv->string = buffer;
    sv->size = size;
    return sv;
}

/* Create a new RAW lily_string_val. The newly-made string will hold 'size'
   bytes of 'source'. 'source' is expected to NOT be \0 terminated, and thus
   'size' SHOULD NOT include any \0 termination. Instead, the \0 termination
   will */
lily_string_val *lily_new_raw_string_sized(const char *source, int len)
{
    char *buffer = lily_malloc(len + 1);
    memcpy(buffer, source, len);
    buffer[len] = '\0';

    return new_sv(buffer, len);
}

/* Create a new RAW lily_string_val. The newly-made string shall contain a copy
   of what is inside 'source'. The source is expected to be \0 terminated. */
lily_string_val *lily_new_raw_string(const char *source)
{
    int len = strlen(source);
    char *buffer = lily_malloc(len + 1);
    strcpy(buffer, source);

    return new_sv(buffer, len);
}

/* Create a new value holding a string. That string shall contain a copy of what
   is inside 'source'. The source is expected to be \0 terminated. */
lily_value *lily_new_string(const char *source)
{
    lily_string_val *sv = lily_new_raw_string(source);
    return lily_new_value(VAL_IS_STRING | VAL_IS_DEREFABLE, (lily_raw_value) { sv });
}

/* Create a new value holding a string. That string's contents will be 'len'
   bytes of 'source'. Cloning is done through strncpy, so \0 termination is not
   necessary. */
lily_value *lily_new_string_ncpy(const char *source, int len)
{
    lily_string_val *sv = lily_new_raw_string_sized(source, len);
    return lily_new_value(VAL_IS_STRING | VAL_IS_DEREFABLE, (lily_raw_value) { sv });
}

/* Create a new value holding a string. That string's source will be exactly
   'source'. The string made assumes that it owns 'source' from here on. The
   source given must be \0 terminated. */
lily_value *lily_new_string_take(char *source)
{
    lily_string_val *sv = new_sv(source, strlen(source));
    return lily_new_value(VAL_IS_STRING | VAL_IS_DEREFABLE, (lily_raw_value) { sv });
}

lily_instance_val *lily_new_instance_val()
{
    lily_instance_val *ival = lily_malloc(sizeof(lily_instance_val));
    ival->refcount = 1;
    ival->gc_entry = NULL;
    ival->values = NULL;
    ival->num_values = -1;

    return ival;
}

lily_instance_val *lily_new_enum_1(uint16_t class_id, uint16_t variant_id,
        lily_value *v)
{
    lily_instance_val *iv = lily_new_instance_val();
    iv->values = lily_malloc(sizeof(lily_value));
    iv->values[0] = v;
    iv->num_values = 1;
    iv->variant_id = variant_id;
    iv->instance_id = class_id;

    return iv;
}

static int lily_eq_value_raw(lily_vm_state *, int *, lily_value *,
        lily_value *);

/* This checks of all elements of two (lists, tuples, enums) are equivalent to
   each other. */
static int subvalue_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    lily_list_val *left_list = left->value.list;
    lily_list_val *right_list = right->value.list;
    int ok;
    if (left_list->num_values == right_list->num_values) {
        ok = 1;
        int i;
        for (i = 0;i < left_list->num_values;i++) {
            lily_value *left_item = left_list->elems[i];
            lily_value *right_item = right_list->elems[i];
            (*depth)++;
            if (lily_eq_value_raw(vm, depth, left_item, right_item) == 0) {
                (*depth)--;
                ok = 0;
                break;
            }
            (*depth)--;
        }
    }
    else
        ok = 0;

    return ok;
}

/* Determine if two values are equivalent to each other. */
int lily_eq_value_raw(lily_vm_state *vm, int *depth, lily_value *left, lily_value *right)
{
    int left_tag = left->flags & ~(VAL_IS_DEREFABLE | VAL_IS_GC_TAGGED | VAL_IS_GC_SPECULATIVE);
    int right_tag = right->flags & ~(VAL_IS_DEREFABLE | VAL_IS_GC_TAGGED | VAL_IS_GC_SPECULATIVE);

    if (*depth == 100)
        lily_raise(vm->raiser, lily_RuntimeError, "Infinite loop in comparison.\n");

    if (left_tag != right_tag)
        return 0;
    else if (left_tag & (VAL_IS_INTEGER | VAL_IS_BOOLEAN))
        return left->value.integer == right->value.integer;
    else if (left_tag & VAL_IS_DOUBLE)
        return left->value.doubleval == right->value.doubleval;
    else if (left_tag & VAL_IS_STRING)
        return strcmp(left->value.string->string,
                right->value.string->string) == 0;
    else if (left_tag & VAL_IS_BYTESTRING) {
        lily_string_val *left_sv = left->value.string;
        lily_string_val *right_sv = right->value.string;
        char *left_s = left_sv->string;
        char *right_s = right_sv->string;
        int left_size = left_sv->size;
        return (left_size == right_sv->size &&
                memcmp(left_s, right_s, left_size) == 0);
    }
    else if (left_tag & (VAL_IS_LIST | VAL_IS_TUPLE)) {
        return subvalue_eq(vm, depth, left, right);
    }
    else if (left_tag & VAL_IS_HASH) {
        lily_hash_val *left_hash = left->value.hash;
        lily_hash_val *right_hash = right->value.hash;

        lily_hash_elem *left_iter = left_hash->elem_chain;
        lily_hash_elem *right_iter;
        lily_hash_elem *right_start = right_hash->elem_chain;
        /* Assume success, in case the hash is empty. */
        int ok = 1;
        for (left_iter = left_hash->elem_chain;
             left_iter != NULL;
             left_iter = left_iter->next) {
            (*depth)++;
            ok = 0;
            for (right_iter = right_start;
                 right_iter != NULL;
                 right_iter = right_iter->next) {
                if (left_iter->key_siphash == right_iter->key_siphash) {
                    ok = lily_eq_value_raw(vm, depth, left_iter->elem_key,
                            right_iter->elem_key);
                    ok = ok && lily_eq_value_raw(vm, depth,
                            left_iter->elem_value, right_iter->elem_value);

                    /* Hash keys are unique, so this won't be found again. */
                    if (right_iter == right_start)
                        right_start = right_start->next;

                    break;
                }
            }
            (*depth)--;

            if (ok == 0)
                break;
        }

        return ok;
    }
    else if (left_tag & VAL_IS_DYNAMIC) {
        (*depth)++;
        lily_value *left_value = left->value.dynamic->inner_value;
        lily_value *right_value = right->value.dynamic->inner_value;
        int ok = lily_eq_value_raw(vm, depth, left_value, right_value);
        (*depth)--;

        return ok;
    }
    else if (left_tag & VAL_IS_ENUM) {
        lily_instance_val *left_i = left->value.instance;
        lily_instance_val *right_i = right->value.instance;
        int ok;
        if (left_i->instance_id == right_i->instance_id &&
            left_i->variant_id == right_i->variant_id)
            ok = subvalue_eq(vm, depth, left, right);
        else
            ok = 0;

        return ok;
    }
    else
        /* Everything else gets pointer equality. */
        return left->value.generic == right->value.generic;
}

int lily_eq_value(lily_vm_state *vm, lily_value *left, lily_value *right)
{
    int depth = 0;
    return lily_eq_value_raw(vm, &depth, left, right);
}
