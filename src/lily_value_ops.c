#include <string.h>

#include "lily_vm.h"
#include "lily_core_types.h"

/* The destroy function for hashes is included inside of hash because a couple
   things inside of hash need to be able to destroy elems/clear it out. */
#include "lily_cls_hash.h"

#include "lily_api_alloc.h"
#include "lily_api_value_ops.h"

/***
 *       ___                       _   _
 *      / _ \ _ __   ___ _ __ __ _| |_(_) ___  _ __  ___
 *     | | | | '_ \ / _ \ '__/ _` | __| |/ _ \| '_ \/ __|
 *     | |_| | |_) |  __/ | | (_| | |_| | (_) | | | \__ \
 *      \___/| .__/ \___|_|  \__,_|\__|_|\___/|_| |_|___/
 *           |_|
 */

extern lily_gc_entry *lily_gc_stopper;

static void destroy_list(lily_value *v)
{
    lily_list_val *lv = v->value.list;

    int full_destroy = 1;
    if (lv->gc_entry) {
        if (lv->gc_entry->last_pass == -1) {
            full_destroy = 0;
            lv->gc_entry = lily_gc_stopper;
        }
        else
            lv->gc_entry->value.generic = NULL;
    }

    int i;
    for (i = 0;i < lv->num_values;i++) {
        lily_deref(lv->elems[i]);
        lily_free(lv->elems[i]);
    }

    lily_free(lv->elems);

    if (full_destroy)
        lily_free(lv);
}

static void destroy_string(lily_value *v)
{
    lily_string_val *sv = v->value.string;

    if (sv->string)
        lily_free(sv->string);

    lily_free(sv);
}

static void destroy_function(lily_value *v)
{
    lily_function_val *fv = v->value.function;
    if (fv->gc_entry == lily_gc_stopper)
        return;

    if (fv->upvalues == NULL) {
        lily_free(fv->code);
        lily_free(fv);
    }
    else {
        int full_destroy = 1;

        if (fv->gc_entry) {
            if (fv->gc_entry->last_pass == -1) {
                full_destroy = 0;
                fv->gc_entry = lily_gc_stopper;
            }
            else
                fv->gc_entry->value.generic = NULL;
        }

        lily_value **upvalues = fv->upvalues;
        int count = fv->num_upvalues;
        int i;

        for (i = 0;i < count;i++) {
            lily_value *up = upvalues[i];
            if (up) {
                up->cell_refcount--;

                if (up->cell_refcount == 0) {
                    lily_deref(up);
                    lily_free(up);
                }
            }
        }
        lily_free(upvalues);

        if (full_destroy)
            lily_free(fv);
    }
}

static void destroy_dynamic(lily_value *v)
{
    lily_dynamic_val *dv = v->value.dynamic;

    int full_destroy = 1;
    if (dv->gc_entry) {
        if (dv->gc_entry->last_pass == -1) {
            full_destroy = 0;
            dv->gc_entry = lily_gc_stopper;
        }
        else
            dv->gc_entry->value.generic = NULL;
    }

    lily_deref(dv->inner_value);
    lily_free(dv->inner_value);

    if (full_destroy)
        lily_free(dv);
}

static void destroy_file(lily_value *v)
{
    lily_file_val *filev = v->value.file;

    if (filev->inner_file && filev->is_builtin == 0)
        fclose(filev->inner_file);

    lily_free(filev);
}

void lily_destroy_value(lily_value *v)
{
    int flags = v->flags;
    if (flags & (VAL_IS_LIST | VAL_IS_INSTANCE | VAL_IS_TUPLE | VAL_IS_ENUM))
        destroy_list(v);
    else if (flags & (VAL_IS_STRING | VAL_IS_BYTESTRING))
        destroy_string(v);
    else if (flags & VAL_IS_FUNCTION)
        destroy_function(v);
    else if (flags & VAL_IS_HASH)
        lily_destroy_hash(v);
    else if (flags & VAL_IS_DYNAMIC)
        destroy_dynamic(v);
    else if (flags & VAL_IS_FILE)
        destroy_file(v);
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

/* This assigns 'left' to the value within 'right', but does not give 'right' a
   ref increase. One use case for this is List.pop, which takes the element out
   of the List and places it into the result. The value has been assigned over,
   (so all flags carry), but still has the same # of refs. */
void lily_assign_value_noref(lily_value *left, lily_value *right)
{
    if (left->flags & VAL_IS_DEREFABLE)
        lily_deref(left);

    left->value = right->value;
    left->flags = right->flags;
}

/* Create a copy of a value. It may get a ref. */
lily_value *lily_copy_value(lily_value *input)
{
    if (input->flags & VAL_IS_DEREFABLE)
        input->value.generic->refcount++;

    lily_value *result = lily_malloc(sizeof(lily_value));
    result->flags = input->flags;
    result->value = input->value;

    return result;
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

/***
 *      __  __
 *     |  \/  | _____   _____  ___
 *     | |\/| |/ _ \ \ / / _ \/ __|
 *     | |  | | (_) \ V /  __/\__ \
 *     |_|  |_|\___/ \_/ \___||___/
 *
 */

#define MOVE_FN(name, in_type, field, f) \
void lily_move_##name(lily_value *v, in_type z) \
{ \
    if (v->flags & VAL_IS_DEREFABLE) \
        lily_deref(v); \
\
    v->value.field = z; \
    v->flags = f; \
}

#define MOVE_FN_F(name, in_type, field, type_flag) \
void lily_move_##name##_f(uint32_t f, lily_value *v, in_type z) \
{ \
    if (v->flags & VAL_IS_DEREFABLE) \
        lily_deref(v); \
\
    v->value.field = z; \
    v->flags = (f | type_flag); \
}

MOVE_FN  (boolean,        int64_t,             integer,   VAL_IS_BOOLEAN)
MOVE_FN  (double,         double,              doubleval, VAL_IS_DOUBLE)
MOVE_FN  (dynamic,        lily_dynamic_val *,  dynamic,   VAL_IS_DYNAMIC  | VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE)
MOVE_FN_F(enum,           lily_instance_val *, instance,  VAL_IS_ENUM)
MOVE_FN  (file,           lily_file_val *,     file,      VAL_IS_FILE     | VAL_IS_DEREFABLE)
MOVE_FN  (foreign,        lily_foreign_val *,  foreign,   VAL_IS_FOREIGN  | VAL_IS_DEREFABLE)
MOVE_FN_F(function,       lily_function_val *, function,  VAL_IS_FUNCTION)
MOVE_FN_F(hash,           lily_hash_val *,     hash,      VAL_IS_HASH)
MOVE_FN  (integer,        int64_t,             integer,   VAL_IS_INTEGER)
MOVE_FN_F(instance,       lily_instance_val *, instance,  VAL_IS_INSTANCE)
MOVE_FN_F(list,           lily_list_val *,     list,      VAL_IS_LIST)
MOVE_FN  (string,         lily_string_val *,   string,    VAL_IS_STRING   | VAL_IS_DEREFABLE)

/***
 *       ____                _
 *      / ___|_ __ ___  __ _| |_ ___
 *     | |   | '__/ _ \/ _` | __/ _ \
 *     | |___| | |  __/ (_| | ||  __/
 *      \____|_|  \___|\__,_|\__\___|
 *
 */

/** These functions are used to create new raw values. For Lily to use a raw
    value, that value must be moved into a register so that Lily can see it. The
    above move functions are for doing that. **/

/* Create a new value with no contents. This is deemed safe from invalid reads
   because the vm won't touch the data part of a value without first checking
   the flags. */
lily_value *lily_new_empty_value(void)
{
    lily_value *result = lily_malloc(sizeof(lily_value));
    result->flags = 0;

    return result;
}

/* Create a new Dynamic value. The contents of the Dynamic are set to an empty
   raw value that is ready to be moved in. */
lily_dynamic_val *lily_new_dynamic_val(void)
{
    lily_dynamic_val *d = lily_malloc(sizeof(lily_dynamic_val));

    d->inner_value = lily_new_empty_value();
    d->gc_entry = NULL;
    d->refcount = 1;

    return d;
}

/* Create a new File value. The 'inner_file' given is expected to be a valid,
   already-opened C file. 'mode' should be a valid mode according to what fopen
   expects. The read/write bits on the File are set based on parsing 'mode'.
   Lily will close the File when it is deref'd/collected if it is open. */
lily_file_val *lily_new_file_val(FILE *inner_file, const char *mode)
{
    lily_file_val *filev = lily_malloc(sizeof(lily_file_val));

    int plus = strchr(mode, '+') != NULL;

    filev->refcount = 1;
    filev->inner_file = inner_file;
    filev->read_ok = (*mode == 'r' || plus);
    filev->write_ok = (*mode == 'w' || plus);
    filev->is_builtin = 0;

    return filev;
}

/* This creates a new function value that wraps over a foreign (C) function.
   This is meant to be used at parse-time, not vm-time. */
lily_function_val *lily_new_foreign_function_val(lily_foreign_func func,
        const char *class_name, const char *name)
{
    lily_function_val *f = lily_malloc(sizeof(lily_function_val));

    f->refcount = 1;
    f->class_name = class_name;
    f->trace_name = name;
    f->foreign_func = func;
    f->code = NULL;
    f->num_upvalues = 0;
    f->upvalues = NULL;
    f->gc_entry = NULL;
    f->reg_count = -1;
    return f;
}

/* This creates a new function value representing a native function. This is
   meant to be used at parse-time, not vm-time. */
lily_function_val *lily_new_native_function_val(char *class_name,
        char *name)
{
    lily_function_val *f = lily_malloc(sizeof(lily_function_val));

    f->refcount = 1;
    f->class_name = class_name;
    f->trace_name = name;
    f->foreign_func = NULL;
    f->code = NULL;
    f->num_upvalues = 0;
    f->upvalues = NULL;
    f->gc_entry = NULL;
    f->reg_count = -1;
    return f;
}

/* This clones the data inside of 'to_copy'. This is used mostly internally as a
   prelude to closure creation. */
lily_function_val *lily_new_function_copy(lily_function_val *to_copy)
{
    lily_function_val *f = malloc(sizeof(lily_function_val));

    *f = *to_copy;
    return f;
}


lily_list_val *lily_new_list_val(void)
{
    lily_list_val *lv = lily_malloc(sizeof(lily_list_val));
    lv->refcount = 1;
    lv->gc_entry = NULL;
    lv->elems = NULL;
    lv->num_values = -1;
    lv->extra_space = 0;

    return lv;
}

lily_hash_val *lily_new_hash_val(void)
{
    lily_hash_val *h = lily_malloc(sizeof(lily_hash_val));

    h->refcount = 1;
    h->iter_count = 0;
    h->num_elems = 0;
    h->elem_chain = NULL;
    return h;
}

lily_instance_val *lily_new_instance_val(void)
{
    lily_instance_val *ival = lily_malloc(sizeof(lily_instance_val));
    ival->refcount = 1;
    ival->gc_entry = NULL;
    ival->values = NULL;
    ival->num_values = -1;

    return ival;
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
    lily_value *result = lily_new_empty_value();
    lily_move_string(result, lily_new_raw_string(source));
    return result;
}

/* Create a new value holding a string. That string's contents will be 'len'
   bytes of 'source'. Cloning is done through strncpy, so \0 termination is not
   necessary. */
lily_value *lily_new_string_ncpy(const char *source, int len)
{
    lily_value *result = lily_new_empty_value();
    lily_move_string(result, lily_new_raw_string_sized(source, len));
    return result;
}

/* Create a new value holding a string. That string's source will be exactly
   'source'. The string made assumes that it owns 'source' from here on. The
   source given must be \0 terminated. */
lily_value *lily_new_string_take(char *source)
{
    lily_value *result = lily_new_empty_value();
    lily_move_string(result, new_sv(source, strlen(source)));
    return result;
}

static lily_instance_val *new_enum_1(uint16_t class_id, uint16_t variant_id,
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

/* This creates a new enum representing a Some that wraps around the value
   given. The value is considered owned by the Some, and will receive a deref
   when the Some is destroyed. */
lily_instance_val *lily_new_some(lily_value *v)
{
    return new_enum_1(SYM_CLASS_OPTION, SOME_VARIANT_ID, v);
}

/* Since None has no arguments, it has a backing literal to represent it. This
   dives into the vm's class table to get the backing literal of the None. */
lily_instance_val *lily_get_none(lily_vm_state *vm)
{
    lily_class *opt_class = vm->class_table[SYM_CLASS_OPTION];
    lily_variant_class *none_cls = opt_class->variant_members[NONE_VARIANT_ID];
    return none_cls->default_value->value.instance;
}

/* This creates a new Left of an Either that wraps over the value provided, and
   takes ownership of it. */
lily_instance_val *lily_new_left(lily_value *v)
{
    return new_enum_1(SYM_CLASS_EITHER, LEFT_VARIANT_ID, v);
}

/* This creates a new Right of an Either that wraps over the value provided, and
   takes ownership of it. */
lily_instance_val *lily_new_right(lily_value *v)
{
    return new_enum_1(SYM_CLASS_EITHER, RIGHT_VARIANT_ID, v);
}
