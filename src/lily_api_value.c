#include <string.h>

#include "lily_value_structs.h"
#include "lily_move.h"
#include "lily_vm.h"

#include "lily_api_alloc.h"
#include "lily_api_value.h"
#include "lily_api_value_flags.h"
#include "lily_api_vm.h"

#define DEFINE_SETTERS(name, action, ...) \
void lily_##name##_boolean(__VA_ARGS__, int v) \
{ lily_move_boolean(source->action, v); } \
void lily_##name##_bytestring(__VA_ARGS__, lily_string_val * v) \
{ lily_move_bytestring(source->action, v); } \
void lily_##name##_double(__VA_ARGS__, double v) \
{ lily_move_double(source->action, v); } \
void lily_##name##_empty_variant(__VA_ARGS__, lily_instance_val * v) \
{ lily_move_empty_variant(source->action, v); } \
void lily_##name##_file(__VA_ARGS__, lily_file_val * v) \
{ lily_move_file(source->action, v); } \
void lily_##name##_foreign(__VA_ARGS__, lily_foreign_val * v) \
{ lily_move_foreign_f(MOVE_DEREF_SPECULATIVE, source->action, v); } \
void lily_##name##_filled_variant(__VA_ARGS__, lily_instance_val * v) \
{ lily_move_enum_f(MOVE_DEREF_SPECULATIVE, source->action, v); } \
void lily_##name##_hash(__VA_ARGS__, lily_hash_val * v) \
{ lily_move_hash_f(MOVE_DEREF_SPECULATIVE, source->action, v); } \
void lily_##name##_instance(__VA_ARGS__, lily_instance_val * v) \
{ lily_move_instance_f(MOVE_DEREF_SPECULATIVE, source->action, v); } \
void lily_##name##_integer(__VA_ARGS__, int64_t v) \
{ lily_move_integer(source->action, v); } \
void lily_##name##_list(__VA_ARGS__, lily_list_val * v) \
{ lily_move_list_f(MOVE_DEREF_SPECULATIVE, source->action, v); } \
void lily_##name##_string(__VA_ARGS__, lily_string_val * v) \
{ lily_move_string(source->action, v); } \
void lily_##name##_tuple(__VA_ARGS__, lily_list_val * v) \
{ lily_move_tuple_f(MOVE_DEREF_SPECULATIVE, source->action, v); } \
void lily_##name##_value(__VA_ARGS__, lily_value * v) \
{ lily_assign_value(source->action, v); } \

#define DEFINE_GETTERS(name, action, ...) \
int lily_##name##_boolean(__VA_ARGS__) \
{ return source->action->value.integer; } \
lily_string_val *lily_##name##_bytestring(__VA_ARGS__) \
{ return source->action->value.string; } \
double lily_##name##_double(__VA_ARGS__) \
{ return source->action->value.doubleval; } \
lily_file_val *lily_##name##_file(__VA_ARGS__) \
{ return source->action->value.file; } \
FILE *lily_##name##_file_raw(__VA_ARGS__) \
{ return source->action->value.file->inner_file; } \
lily_function_val *lily_##name##_function(__VA_ARGS__) \
{ return source->action->value.function; } \
lily_hash_val *lily_##name##_hash(__VA_ARGS__) \
{ return source->action->value.hash; } \
lily_generic_val *lily_##name##_generic(__VA_ARGS__) \
{ return source->action->value.generic; } \
lily_instance_val *lily_##name##_instance(__VA_ARGS__) \
{ return source->action->value.instance; } \
int64_t lily_##name##_integer(__VA_ARGS__) \
{ return source->action->value.integer; } \
lily_list_val *lily_##name##_list(__VA_ARGS__) \
{ return source->action->value.list; } \
lily_string_val *lily_##name##_string(__VA_ARGS__) \
{ return source->action->value.string; } \
char *lily_##name##_string_raw(__VA_ARGS__) \
{ return source->action->value.string->string; } \
lily_value *lily_##name##_value(__VA_ARGS__) \
{ return source->action; }

#define DEFINE_BOTH(name, action, ...) \
DEFINE_SETTERS(name##_set, action, __VA_ARGS__) \
DEFINE_GETTERS(name, action, __VA_ARGS__)

DEFINE_BOTH(instance, values[i], lily_instance_val *source, int i)
DEFINE_BOTH(dynamic, inner_value, lily_dynamic_val *source)
DEFINE_BOTH(list, elems[i], lily_list_val *source, int i)
DEFINE_BOTH(variant, values[i], lily_instance_val *source, int i)

DEFINE_SETTERS(return, call_chain->prev->return_target, lily_vm_state *source)

lily_value *lily_instance_get(lily_instance_val *source, int i)
{
    return source->values[i];
}

/* Special-cased returns */

void lily_return_value_noref(lily_vm_state *vm, lily_value *v)
{
    lily_assign_value_noref(vm->call_chain->prev->return_target, v);
}

void lily_result_return(lily_vm_state *vm)
{
    lily_value *r = vm->regs_from_main[vm->num_registers - 1];
    lily_assign_value_noref(vm->call_chain->prev->return_target, r);
    r->flags = 0;
    vm->num_registers--;
}

/* Argument and result operations */

DEFINE_GETTERS(arg, vm_regs[index], lily_vm_state *source, int index)
DEFINE_GETTERS(result, call_chain->return_target, lily_vm_state *source)

int lily_arg_count(lily_vm_state *vm)
{
    return vm->call_chain->regs_used;
}

/* Stack operations
   Push operations are located within the vm, so that stack growing can remain
   internal to the vm. */

lily_value *lily_pop_value(lily_vm_state *vm)
{
    vm->num_registers--;
    return vm->regs_from_main[vm->num_registers];
}

void lily_drop_value(lily_vm_state *vm)
{
    vm->num_registers--;
    lily_value *z = vm->regs_from_main[vm->num_registers];
    lily_deref(z);
    z->flags = 0;
}

/* Raw value creation functions. */

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
    d->refcount = 0;

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

    filev->refcount = 0;
    filev->inner_file = inner_file;
    filev->read_ok = (*mode == 'r' || plus);
    filev->write_ok = (*mode == 'w' || plus);
    filev->is_builtin = 0;

    return filev;
}

lily_list_val *lily_new_list_val_n(int initial)
{
    lily_list_val *lv = lily_malloc(sizeof(lily_list_val));
    lv->elems = lily_malloc(initial * sizeof(lily_value *));
    lv->refcount = 0;
    lv->num_values = initial;
    lv->extra_space = 0;

    int i;
    for (i = 0;i < initial;i++) {
        lily_value *elem = lily_malloc(sizeof(lily_value));
        elem->flags = 0;
        lv->elems[i] = elem;
    }

    return lv;
}

lily_hash_val *lily_new_hash_val(void)
{
    lily_hash_val *h = lily_malloc(sizeof(lily_hash_val));

    h->refcount = 0;
    h->iter_count = 0;
    h->num_elems = 0;
    h->elem_chain = NULL;
    return h;
}

lily_instance_val *lily_new_instance_val_n_of(int initial, uint16_t instance_id)
{
    lily_instance_val *ival = lily_malloc(sizeof(lily_instance_val));

    ival->values = lily_malloc(initial * sizeof(lily_value *));
    ival->refcount = 0;
    ival->gc_entry = NULL;
    ival->num_values = initial;
    ival->instance_id = instance_id;

    int i;
    for (i = 0;i < initial;i++) {
        lily_value *v = lily_malloc(sizeof(lily_value));
        v->flags = 0;
        ival->values[i] = v;
    }

    return ival;
}

static lily_string_val *new_sv(char *buffer, int size)
{
    lily_string_val *sv = lily_malloc(sizeof(lily_string_val));
    sv->refcount = 0;
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

/* Create a new raw string that takes ownership of 'source'. */
lily_string_val *lily_new_raw_string_take(char *source)
{
    return new_sv(source, strlen(source));
}

/* Create a new value holding a string. That string shall contain a copy of what
   is inside 'source'. The source is expected to be \0 terminated. */
lily_value *lily_new_string(const char *source)
{
    lily_value *result = lily_new_empty_value();
    lily_move_string(result, lily_new_raw_string(source));
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

/* Since None has no arguments, it has a backing literal to represent it. This
   dives into the vm's class table to get the backing literal of the None. */
lily_instance_val *lily_get_none(lily_vm_state *vm)
{
    lily_variant_class *none_cls;
    none_cls = (lily_variant_class *)vm->class_table[SYM_CLASS_NONE];
    return none_cls->default_value->value.instance;
}

lily_instance_val *lily_new_some(void)
{
    return lily_new_instance_val_n_of(1, SYM_CLASS_SOME);
}

lily_instance_val *lily_new_left(void)
{
    return lily_new_instance_val_n_of(1, SYM_CLASS_LEFT);
}

lily_instance_val *lily_new_right(void)
{
    return lily_new_instance_val_n_of(1, SYM_CLASS_RIGHT);
}

/* Simple per-type operations. */

char *lily_bytestring_get_raw(lily_string_val *sv)
{
    return sv->string;
}

int lily_bytestring_length(lily_string_val *sv)
{
    return sv->size;
}

FILE *lily_file_get_raw(lily_file_val *fv)
{
    return fv->inner_file;
}

void lily_file_ensure_writeable(lily_vm_state *vm, lily_file_val *filev)
{
    if (filev->inner_file == NULL)
        lily_vm_raise(vm, SYM_CLASS_IOERROR, "IO operation on closed file.");

    if (filev->write_ok == 0)
        lily_vm_raise(vm, SYM_CLASS_IOERROR, "File not open for writing.");
}

void lily_file_ensure_readable(lily_vm_state *vm, lily_file_val *filev)
{
    if (filev->inner_file == NULL)
        lily_vm_raise(vm, SYM_CLASS_IOERROR, "IO operation on closed file.");

    if (filev->read_ok == 0)
        lily_vm_raise(vm, SYM_CLASS_IOERROR, "File not open for reading.");
}

char *lily_string_get_raw(lily_string_val *sv)
{
    return sv->string;
}

int lily_string_length(lily_string_val *sv)
{
    return sv->size;
}

int lily_list_num_values(lily_list_val *lv)
{
    return lv->num_values;
}

/* Operations */

extern void lily_destroy_hash(lily_value *);
extern lily_gc_entry *lily_gc_stopper;

static void destroy_instance(lily_value *v)
{
    lily_instance_val *iv = v->value.instance;
    if (iv->gc_entry == lily_gc_stopper)
        return;

    int full_destroy = 1;
    if (iv->gc_entry) {
        if (iv->gc_entry->last_pass == -1) {
            full_destroy = 0;
            iv->gc_entry = lily_gc_stopper;
        }
        else
            iv->gc_entry->value.generic = NULL;
    }

    int i;
    for (i = 0;i < iv->num_values;i++) {
        lily_deref(iv->values[i]);
        lily_free(iv->values[i]);
    }

    lily_free(iv->values);

    if (full_destroy)
        lily_free(iv);
}

static void destroy_list(lily_value *v)
{

    lily_list_val *lv = v->value.list;

    int i;
    for (i = 0;i < lv->num_values;i++) {
        lily_deref(lv->elems[i]);
        lily_free(lv->elems[i]);
    }

    lily_free(lv->elems);
    lily_free(lv);
}

static void destroy_string(lily_value *v)
{
    lily_string_val *sv = v->value.string;

    lily_free(sv->string);
    lily_free(sv);
}

static void destroy_function(lily_value *v)
{
    lily_function_val *fv = v->value.function;
    if (fv->gc_entry == lily_gc_stopper)
        return;

    if (fv->num_upvalues == (uint16_t)-1) {
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
    if (dv->gc_entry == lily_gc_stopper)
        return;

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
    if (flags & (VAL_IS_LIST | VAL_IS_TUPLE))
        destroy_list(v);
    else if (flags & (VAL_IS_INSTANCE |VAL_IS_ENUM))
        destroy_instance(v);
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
        v->value.foreign->destroy_func(v->value.generic);
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
        lily_raise(vm->raiser, lily_RuntimeError, "Infinite loop in comparison.");

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
        if (left_i->instance_id == right_i->instance_id)
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
