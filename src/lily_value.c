#include <string.h>

#include "lily_alloc.h"
#include "lily_vm.h"
#include "lily_value.h"
#include "lily_core_types.h"

/* This is for the gc + destroy funcs. :( */

#include "lily_cls_string.h"
#include "lily_cls_list.h"
#include "lily_cls_hash.h"
#include "lily_cls_dynamic.h"
#include "lily_cls_function.h"
#include "lily_cls_file.h"

void destroy_value(lily_value *v)
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
            destroy_value(value);
    }
}

/* This calls deref on the raw part of a value. This should not be used when a
   proper value is available. */
void lily_deref_raw(lily_type *type, lily_raw_value raw)
{
    lily_value v;
    v.flags = type->cls->move_flags | VAL_IS_DEREFABLE;
    v.value = raw;

    lily_deref(&v);
}

inline lily_value *lily_new_value(uint64_t flags, lily_raw_value raw)
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

void lily_move(lily_value *left, lily_raw_value raw_right, int move_flags)
{
    if (left->flags & VAL_IS_DEREFABLE)
        lily_deref(left);

    left->value = raw_right;
    left->flags = move_flags;
}

/* Create a copy of a value. It may get a ref. */
lily_value *lily_copy_value(lily_value *input)
{
    if (input->flags & VAL_IS_DEREFABLE)
        input->value.generic->refcount++;

    return lily_new_value(input->flags, input->value);
}

/* Create a new value holding a string. That string shall contain a copy of what
   is inside 'source'. The source is expected to be \0 terminated. */
lily_value *lily_new_string(const char *source)
{
    lily_string_val *sv = lily_malloc(sizeof(lily_string_val));
    int len = strlen(source);
    sv->refcount = 1;
    sv->string = lily_malloc(len + 1);
    strcpy(sv->string, source);
    sv->size = len;
    return lily_new_value(VAL_IS_STRING | VAL_IS_DEREFABLE, (lily_raw_value)sv);
}

/* Create a new value holding a string. That string's contents will be 'len'
   bytes of 'source'. Cloning is done through strncpy, so \0 termination is not
   necessary. */
lily_value *lily_new_string_ncpy(const char *source, int len)
{
    lily_string_val *sv = lily_malloc(sizeof(lily_string_val));
    sv->refcount = 1;
    sv->string = lily_malloc(len);
    strncpy(sv->string, source, len);
    sv->string[len] = '\0';
    sv->size = len - 1;
    return lily_new_value(VAL_IS_STRING | VAL_IS_DEREFABLE, (lily_raw_value)sv);
}

/* Create a new value holding a string. That string's source will be exactly
   'source'. The string made assumes that it owns 'source' from here on. The
   source given must be \0 terminated. */
lily_value *lily_new_string_take(char *source)
{
    lily_string_val *sv = lily_malloc(sizeof(lily_string_val));
    int len = strlen(source);
    sv->refcount = 1;
    sv->string = source;
    sv->size = len;
    return lily_new_value(VAL_IS_STRING | VAL_IS_DEREFABLE, (lily_raw_value)sv);
}

inline lily_instance_val *lily_new_instance_val()
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

static int lily_value_eq_raw(lily_vm_state *, int *, lily_value *,
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
            if (lily_value_eq_raw(vm, depth, left_item, right_item) == 0) {
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
int lily_value_eq_raw(lily_vm_state *vm, int *depth, lily_value *left, lily_value *right)
{
    int left_tag = left->flags & ~(VAL_IS_DEREFABLE | VAL_IS_GC_TAGGED);
    int right_tag = right->flags & ~(VAL_IS_DEREFABLE | VAL_IS_GC_TAGGED);

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
                    ok = lily_value_eq_raw(vm, depth, left_iter->elem_key,
                            right_iter->elem_key);
                    ok = ok && lily_value_eq_raw(vm, depth,
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
        int ok = lily_value_eq_raw(vm, depth, left_value, right_value);
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

int lily_value_eq(lily_vm_state *vm, lily_value *left, lily_value *right)
{
    int depth = 0;
    return lily_value_eq_raw(vm, &depth, left, right);
}

void lily_collect_value(lily_value *v)
{
    int flags = v->flags;

    if (flags & (VAL_IS_LIST | VAL_IS_INSTANCE | VAL_IS_TUPLE | VAL_IS_ENUM))
        lily_gc_collect_list(v);
    else if (flags & VAL_IS_HASH)
        lily_gc_collect_hash(v);
    else if (flags & VAL_IS_DYNAMIC)
        lily_gc_collect_dynamic(v);
    else if (flags & VAL_IS_FUNCTION)
        lily_gc_collect_function(v);
    else if (flags & (VAL_IS_STRING | VAL_IS_BYTESTRING))
        lily_deref(v);
}
