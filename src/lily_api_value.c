#include <string.h>

#include "lily_value_structs.h"
#include "lily_move.h"
#include "lily_vm.h"
#include "lily_value_flags.h"

#include "lily_api_alloc.h"
#include "lily_api_value.h"

#define DEFINE_SETTERS(name, action, ...) \
void lily_##name##_boolean(__VA_ARGS__, int v) \
{ lily_move_boolean(source->action, v); } \
void lily_##name##_byte(__VA_ARGS__, uint8_t v) \
{ lily_move_byte(source->action, v); } \
void lily_##name##_bytestring(__VA_ARGS__, lily_bytestring_val * v) \
{ lily_move_bytestring(source->action, v); } \
void lily_##name##_double(__VA_ARGS__, double v) \
{ lily_move_double(source->action, v); } \
void lily_##name##_empty_variant(__VA_ARGS__, uint16_t f) \
{ lily_move_empty_variant(f, source->action); } \
void lily_##name##_file(__VA_ARGS__, lily_file_val * v) \
{ lily_move_file(source->action, v); } \
void lily_##name##_foreign(__VA_ARGS__, uint16_t f, lily_foreign_val * v) \
{ lily_move_foreign_f(f | MOVE_DEREF_SPECULATIVE, source->action, v); } \
void lily_##name##_hash(__VA_ARGS__, lily_hash_val * v) \
{ lily_move_hash_f(MOVE_DEREF_SPECULATIVE, source->action, v); } \
void lily_##name##_instance(__VA_ARGS__, uint16_t f, lily_instance_val * v) \
{ lily_move_instance_f(f | MOVE_DEREF_SPECULATIVE, source->action, v); } \
void lily_##name##_integer(__VA_ARGS__, int64_t v) \
{ lily_move_integer(source->action, v); } \
void lily_##name##_list(__VA_ARGS__, lily_list_val * v) \
{ lily_move_list_f(MOVE_DEREF_SPECULATIVE, source->action, v); } \
void lily_##name##_string(__VA_ARGS__, lily_string_val * v) \
{ lily_move_string(source->action, v); } \
void lily_##name##_tuple(__VA_ARGS__, lily_tuple_val * v) \
{ lily_move_tuple_f(MOVE_DEREF_SPECULATIVE, source->action, v); } \
void lily_##name##_unit(__VA_ARGS__) \
{ lily_move_unit(source->action); } \
void lily_##name##_value(__VA_ARGS__, lily_value * v) \
{ lily_value_assign(source->action, v); } \
void lily_##name##_variant(__VA_ARGS__, uint16_t f, lily_variant_val * v) \
{ lily_move_variant_f(f | MOVE_DEREF_SPECULATIVE, source->action, v); } \

#define DEFINE_GETTERS(name, action, ...) \
int lily_##name##_boolean(__VA_ARGS__) \
{ return source->action->value.integer; } \
uint8_t lily_##name##_byte(__VA_ARGS__) \
{ return source->action->value.integer; } \
lily_bytestring_val *lily_##name##_bytestring(__VA_ARGS__) \
{ return (lily_bytestring_val *)source->action->value.string; } \
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
lily_tuple_val *lily_##name##_tuple(__VA_ARGS__) \
{ return (lily_tuple_val *)source->action->value.list; } \
lily_value *lily_##name##_value(__VA_ARGS__) \
{ return source->action; } \
lily_variant_val *lily_##name##_variant(__VA_ARGS__) \
{ return (lily_variant_val *)source->action->value.instance; } \

#define DEFINE_BOTH(name, action, ...) \
DEFINE_SETTERS(name##_set, action, __VA_ARGS__) \
DEFINE_GETTERS(name, action, __VA_ARGS__)

DEFINE_BOTH(instance, values[i], lily_instance_val *source, int i)
DEFINE_BOTH(dynamic, inner_value, lily_dynamic_val *source)
DEFINE_BOTH(list, elems[i], lily_list_val *source, int i)
DEFINE_BOTH(tuple, elems[i], lily_tuple_val *source, int i)
DEFINE_BOTH(variant, values[i], lily_variant_val *source, int i)

DEFINE_SETTERS(return, call_chain->prev->return_target, lily_vm_state *source)

/* Special-cased returns */

void lily_return_value_noref(lily_state *s, lily_value *v)
{
    lily_value_assign_noref(s->call_chain->prev->return_target, v);
}

void lily_result_return(lily_state *s)
{
    lily_value *r = s->regs_from_main[s->num_registers - 1];
    lily_value_assign_noref(s->call_chain->prev->return_target, r);
    r->flags = 0;
    s->num_registers--;
}

/* Argument and result operations */

DEFINE_GETTERS(arg, vm_regs[index], lily_vm_state *source, int index)
DEFINE_GETTERS(result, call_chain->return_target, lily_vm_state *source)

int lily_arg_class_id(lily_state *s, int index)
{
    return s->vm_regs[index]->class_id;
}

int lily_arg_count(lily_state *s)
{
    return s->call_chain->regs_used;
}

int lily_arg_instance_for_id(lily_state *s, int index, lily_instance_val **iv)
{
    lily_value *v = s->vm_regs[index];
    *iv = v->value.instance;
    return v->class_id;
}

int lily_arg_variant_for_id(lily_state *s, int index, lily_variant_val **iv)
{
    lily_value *v = s->vm_regs[index];
    *iv = (lily_variant_val *)v->value.instance;
    return v->class_id;
}

/* Stack operations
   Push operations are located within the vm, so that stack growing can remain
   internal to the vm. */

lily_value *lily_take_value(lily_state *s)
{
    s->num_registers--;
    return s->regs_from_main[s->num_registers];
}

void lily_pop_value(lily_state *s)
{
    s->num_registers--;
    lily_value *z = s->regs_from_main[s->num_registers];
    lily_deref(z);
    z->flags = 0;
}

/* Value creation functions. */

lily_value *lily_new_value_of_byte(uint8_t byte)
{
    lily_value *v = lily_malloc(sizeof(lily_value));

    v->flags = LILY_BYTE_ID | VAL_IS_DEREFABLE;
    v->value.integer = byte;
    return v;
}

lily_value *lily_new_value_of_bytestring(lily_bytestring_val *bv)
{
    lily_value *v = lily_malloc(sizeof(lily_value));

    bv->refcount++;
    v->flags = LILY_BYTESTRING_ID | VAL_IS_DEREFABLE;
    v->value.string = (lily_string_val *)bv;
    return v;
}

lily_value *lily_new_value_of_double(double d)
{
    lily_value *v = lily_malloc(sizeof(lily_value));

    v->flags = LILY_DOUBLE_ID;
    v->value.doubleval = d;
    return v;
}

lily_value *lily_new_value_of_enum(uint16_t id, lily_instance_val *iv)
{
    lily_value *v = lily_malloc(sizeof(lily_value));

    iv->refcount++;
    v->flags = id | VAL_IS_ENUM | VAL_IS_DEREFABLE;
    v->value.instance = iv;
    return v;
}

lily_value *lily_new_value_of_file(lily_file_val *fv)
{
    lily_value *v = lily_malloc(sizeof(lily_value));

    fv->refcount++;
    v->flags = LILY_FILE_ID | VAL_IS_DEREFABLE;
    v->value.file = fv;
    return v;
}

lily_value *lily_new_value_of_hash(lily_hash_val *hv)
{
    lily_value *v = lily_malloc(sizeof(lily_value));

    hv->refcount++;
    v->flags = LILY_HASH_ID | VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE;
    v->value.hash = hv;
    return v;
}

lily_value *lily_new_value_of_instance(uint16_t id, lily_instance_val *iv)
{
    lily_value *v = lily_malloc(sizeof(lily_value));

    iv->refcount++;
    v->flags = id | VAL_IS_INSTANCE | VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE;
    v->value.instance = iv;
    return v;
}

lily_value *lily_new_value_of_integer(int64_t i)
{
    lily_value *v = lily_malloc(sizeof(lily_value));

    v->flags = LILY_INTEGER_ID;
    v->value.integer = i;
    return v;
}

lily_value *lily_new_value_of_list(lily_list_val *lv)
{
    lily_value *v = lily_malloc(sizeof(lily_value));

    lv->refcount++;
    v->flags = LILY_LIST_ID | VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE;
    v->value.list = lv;
    return v;
}

lily_value *lily_new_value_of_string(lily_string_val *sv)
{
    lily_value *v = lily_malloc(sizeof(lily_value));

    sv->refcount++;
    v->flags = LILY_STRING_ID | VAL_IS_DEREFABLE;
    v->value.string = sv;
    return v;
}

lily_value *lily_new_value_of_string_raw(const char *str)
{
    lily_value *v = lily_malloc(sizeof(lily_value));
    lily_string_val *sv = lily_new_string(str);

    sv->refcount++;
    v->flags = LILY_STRING_ID | VAL_IS_DEREFABLE;
    v->value.string = sv;
    return v;
}

/* Raw value creation functions. */

/* Create a new Dynamic value. The contents of the Dynamic are set to an empty
   raw value that is ready to be moved in. */
lily_dynamic_val *lily_new_dynamic(void)
{
    lily_dynamic_val *d = lily_malloc(sizeof(lily_dynamic_val));
    lily_value *v = lily_malloc(sizeof(lily_value));

    v->flags = 0;
    d->inner_value = v;
    d->gc_entry = NULL;
    d->refcount = 0;

    return d;
}

/* Create a new File value. The 'inner_file' given is expected to be a valid,
   already-opened C file. 'mode' should be a valid mode according to what fopen
   expects. The read/write bits on the File are set based on parsing 'mode'.
   Lily will close the File when it is deref'd/collected if it is open. */
lily_file_val *lily_new_file(FILE *inner_file, const char *mode)
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

lily_list_val *lily_new_list(int initial)
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

lily_instance_val *lily_new_instance(int initial)
{
    lily_instance_val *ival = lily_malloc(sizeof(lily_instance_val));

    ival->values = lily_malloc(initial * sizeof(lily_value *));
    ival->refcount = 0;
    ival->gc_entry = NULL;
    ival->num_values = initial;
    ival->ctor_need = 0;

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
lily_string_val *lily_new_string_sized(const char *source, int len)
{
    char *buffer = lily_malloc(len + 1);
    memcpy(buffer, source, len);
    buffer[len] = '\0';

    return new_sv(buffer, len);
}

/* Create a new RAW lily_string_val. The newly-made string shall contain a copy
   of what is inside 'source'. The source is expected to be \0 terminated. */
lily_string_val *lily_new_string(const char *source)
{
    int len = strlen(source);
    char *buffer = lily_malloc(len + 1);
    strcpy(buffer, source);

    return new_sv(buffer, len);
}

/* Create a new raw string that takes ownership of 'source'. */
lily_string_val *lily_new_string_take(char *source)
{
    return new_sv(source, strlen(source));
}

lily_tuple_val *lily_new_tuple(int initial)
{
    return (lily_tuple_val *)lily_new_list(initial);
}

lily_variant_val *lily_new_variant(int size)
{
    lily_variant_val *ival = lily_malloc(sizeof(lily_variant_val));

    ival->values = lily_malloc(size * sizeof(lily_value *));
    ival->refcount = 0;
    ival->gc_entry = NULL;
    ival->num_values = size;

    int i;
    for (i = 0;i < size;i++) {
        lily_value *v = lily_malloc(sizeof(lily_value));
        v->flags = 0;
        ival->values[i] = v;
    }

    return ival;
}

/* Simple per-type operations. */

lily_bytestring_val *lily_new_bytestring_sized(const char *source, int len)
{
    return (lily_bytestring_val *)lily_new_string_sized(source, len);
}

lily_bytestring_val *lily_new_bytestring_take(char *source)
{
    return (lily_bytestring_val *)lily_new_string_take(source);
}

lily_bytestring_val *lily_new_bytestring(const char *source)
{
    return (lily_bytestring_val *)lily_new_string(source);
}

char *lily_bytestring_raw(lily_bytestring_val *sv)
{
    return sv->string;
}

int lily_bytestring_length(lily_bytestring_val *sv)
{
    return sv->size;
}

FILE *lily_file_raw(lily_file_val *fv)
{
    return fv->inner_file;
}

void lily_file_ensure_writeable(lily_state *s, lily_file_val *filev)
{
    if (filev->inner_file == NULL)
        lily_IOError(s, "IO operation on closed file.");

    if (filev->write_ok == 0)
        lily_IOError(s, "File not open for writing.");
}

void lily_file_ensure_readable(lily_state *s, lily_file_val *filev)
{
    if (filev->inner_file == NULL)
        lily_IOError(s, "IO operation on closed file.");

    if (filev->read_ok == 0)
        lily_IOError(s, "File not open for reading.");
}

int lily_function_is_foreign(lily_function_val *fv)
{
    return fv->code == NULL;
}

int lily_function_is_native(lily_function_val *fv)
{
    return fv->code != NULL;
}

char *lily_string_raw(lily_string_val *sv)
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
        lily_free(fv->docstring);
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

void lily_value_destroy(lily_value *v)
{
    int class_id = v->class_id;
    if (class_id == LILY_LIST_ID || class_id == LILY_TUPLE_ID)
        destroy_list(v);
    else if (v->flags & (VAL_IS_INSTANCE | VAL_IS_ENUM))
        destroy_instance(v);
    else if (class_id == LILY_STRING_ID || class_id == LILY_BYTESTRING_ID)
        destroy_string(v);
    else if (class_id == LILY_FUNCTION_ID)
        destroy_function(v);
    else if (class_id == LILY_HASH_ID)
        lily_destroy_hash(v);
    else if (class_id == LILY_DYNAMIC_ID)
        destroy_dynamic(v);
    else if (class_id == LILY_FILE_ID)
        destroy_file(v);
    else if (v->flags & VAL_IS_FOREIGN)
        v->value.foreign->destroy_func(v->value.generic);
}

/* Check if the value given is deref-able. If so, hit it with a deref. */
void lily_deref(lily_value *value)
{
    if (value->flags & VAL_IS_DEREFABLE) {
        value->value.generic->refcount--;
        if (value->value.generic->refcount == 0)
            lily_value_destroy(value);
    }
}

/* Assign one value to another. The right may get a ref, and the left may get a
   deref. Both sides are assumed to be equivalent type-wise (only value and
   flags move over). */
void lily_value_assign(lily_value *left, lily_value *right)
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
void lily_value_assign_noref(lily_value *left, lily_value *right)
{
    if (left->flags & VAL_IS_DEREFABLE)
        lily_deref(left);

    left->value = right->value;
    left->flags = right->flags;
}

/* Create a copy of a value. It may get a ref. */
lily_value *lily_value_copy(lily_value *input)
{
    if (input->flags & VAL_IS_DEREFABLE)
        input->value.generic->refcount++;

    lily_value *result = lily_malloc(sizeof(lily_value));
    result->flags = input->flags;
    result->value = input->value;

    return result;
}

static int lily_value_compare_raw(lily_vm_state *, int *, lily_value *,
        lily_value *);

/* This checks of all elements of two (lists, tuples, enums) are equivalent to
   each other. */
static int subvalue_eq(lily_state *s, int *depth, lily_value *left,
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
            if (lily_value_compare_raw(s, depth, left_item, right_item) == 0) {
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
int lily_value_compare_raw(lily_state *s, int *depth, lily_value *left,
        lily_value *right)
{
    int left_tag = left->class_id;
    int right_tag = right->class_id;

    if (*depth == 100)
        lily_RuntimeError(s, "Infinite loop in comparison.");

    if (left_tag != right_tag)
        return 0;
    else if (left_tag == LILY_INTEGER_ID || left_tag == LILY_BOOLEAN_ID)
        return left->value.integer == right->value.integer;
    else if (left_tag == LILY_DOUBLE_ID)
        return left->value.doubleval == right->value.doubleval;
    else if (left_tag == LILY_STRING_ID)
        return strcmp(left->value.string->string,
                right->value.string->string) == 0;
    else if (left_tag == LILY_BYTESTRING_ID) {
        lily_string_val *left_sv = left->value.string;
        lily_string_val *right_sv = right->value.string;
        char *left_s = left_sv->string;
        char *right_s = right_sv->string;
        int left_size = left_sv->size;
        return (left_size == right_sv->size &&
                memcmp(left_s, right_s, left_size) == 0);
    }
    else if (left_tag == LILY_LIST_ID || left_tag == LILY_TUPLE_ID) {
        return subvalue_eq(s, depth, left, right);
    }
    else if (left_tag == LILY_HASH_ID) {
        lily_hash_val *left_hash = left->value.hash;
        lily_hash_val *right_hash = right->value.hash;

        int ok = 1;
        if (left_hash->num_entries != right_hash->num_entries)
            ok = 0;

        if (ok) {
            (*depth)++;
            int i;
            for (i = 0;i < left_hash->num_bins;i++) {
                lily_hash_entry *left = left_hash->bins[i];
                if (left) {
                    lily_value *right = lily_hash_find_value(right_hash,
                            left->boxed_key);

                    if (right == NULL ||
                        lily_value_compare_raw(s, depth, left->record,
                                right) == 0) {
                        ok = 0;
                        break;
                    }
                }
            }
            (*depth)--;
        }
        return ok;
    }
    else if (left_tag == LILY_DYNAMIC_ID) {
        (*depth)++;
        lily_value *left_value = left->value.dynamic->inner_value;
        lily_value *right_value = right->value.dynamic->inner_value;
        int ok = lily_value_compare_raw(s, depth, left_value, right_value);
        (*depth)--;

        return ok;
    }
    else if (left->flags & VAL_IS_ENUM) {
        int ok;
        if (left_tag == right_tag) {
            if (left->value.instance == NULL)
                ok = 1;
            else
                ok = subvalue_eq(s, depth, left, right);
        }
        else
            ok = 0;

        return ok;
    }
    else
        /* Everything else gets pointer equality. */
        return left->value.generic == right->value.generic;
}

int lily_value_compare(lily_state *s, lily_value *left, lily_value *right)
{
    int depth = 0;
    return lily_value_compare_raw(s, &depth, left, right);
}

int lily_value_is_derefable(lily_value *value)
{
    return value->flags & VAL_IS_DEREFABLE;
}

uint16_t lily_value_class_id(lily_value *value)
{
    return value->class_id;
}

void lily_ctor_setup(lily_state *s, lily_instance_val **iv, uint16_t id,
        int initial)
{
    lily_value *v = s->call_chain->prev->return_target;

    if (v->flags & VAL_IS_INSTANCE) {
        lily_instance_val *pending_instance = v->value.instance;
        if (pending_instance->ctor_need != 0) {
            pending_instance->ctor_need = 0;
            *iv = pending_instance;
            return;
        }
    }

    lily_instance_val *new_iv = lily_new_instance(initial);
    lily_return_instance(s, id, new_iv);
    *iv = new_iv;
}
