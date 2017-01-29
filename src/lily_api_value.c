#include <string.h>

#include "lily_value_structs.h"
#include "lily_move.h"
#include "lily_vm.h"
#include "lily_value_flags.h"
#include "lily_alloc.h"

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
void lily_##name##_foreign(__VA_ARGS__, lily_foreign_val * v) \
{ lily_move_foreign_f(MOVE_DEREF_SPECULATIVE, source->action, v); } \
void lily_##name##_hash(__VA_ARGS__, lily_hash_val * v) \
{ lily_move_hash_f(MOVE_DEREF_SPECULATIVE, source->action, v); } \
void lily_##name##_instance(__VA_ARGS__, lily_container_val * v) \
{ lily_move_instance_f(MOVE_DEREF_SPECULATIVE, source->action, v); } \
void lily_##name##_integer(__VA_ARGS__, int64_t v) \
{ lily_move_integer(source->action, v); } \
void lily_##name##_list(__VA_ARGS__, lily_container_val * v) \
{ lily_move_list_f(MOVE_DEREF_SPECULATIVE, source->action, v); } \
void lily_##name##_string(__VA_ARGS__, lily_string_val * v) \
{ lily_move_string(source->action, v); } \
void lily_##name##_tuple(__VA_ARGS__, lily_container_val * v) \
{ lily_move_tuple_f(MOVE_DEREF_SPECULATIVE, source->action, v); } \
void lily_##name##_unit(__VA_ARGS__) \
{ lily_move_unit(source->action); } \
void lily_##name##_value(__VA_ARGS__, lily_value * v) \
{ lily_value_assign(source->action, v); } \
void lily_##name##_variant(__VA_ARGS__, lily_container_val * v) \
{ lily_move_variant_f(MOVE_DEREF_SPECULATIVE, source->action, v); } \

#define DEFINE_GETTERS(name, action, ...) \
int lily_##name##_boolean(__VA_ARGS__) \
{ return source  action->value.integer; } \
uint8_t lily_##name##_byte(__VA_ARGS__) \
{ return source  action->value.integer; } \
lily_bytestring_val *lily_##name##_bytestring(__VA_ARGS__) \
{ return (lily_bytestring_val *)source  action->value.string; } \
lily_container_val *lily_##name##_container(__VA_ARGS__) \
{ return source  action->value.container; } \
double lily_##name##_double(__VA_ARGS__) \
{ return source  action->value.doubleval; } \
lily_file_val *lily_##name##_file(__VA_ARGS__) \
{ return source  action->value.file; } \
FILE *lily_##name##_file_raw(__VA_ARGS__) \
{ return source  action->value.file->inner_file; } \
lily_function_val *lily_##name##_function(__VA_ARGS__) \
{ return source  action->value.function; } \
lily_hash_val *lily_##name##_hash(__VA_ARGS__) \
{ return source  action->value.hash; } \
lily_generic_val *lily_##name##_generic(__VA_ARGS__) \
{ return source  action->value.generic; } \
int64_t lily_##name##_integer(__VA_ARGS__) \
{ return source  action->value.integer; } \
lily_string_val *lily_##name##_string(__VA_ARGS__) \
{ return source  action->value.string; } \
char *lily_##name##_string_raw(__VA_ARGS__) \
{ return source  action->value.string->string; } \
lily_value *lily_##name##_value(__VA_ARGS__) \
{ return source  action; } \

#define DEFINE_PAIR(name, action, ...) \
void lily_##name##_set(__VA_ARGS__, lily_value * v) \
{ lily_value_assign(source->action, v); } \
lily_value *lily_##name##_get(__VA_ARGS__) \
{ return source->action; }

DEFINE_PAIR(boxed_nth, value.container->values[i], lily_value *source, int i)
DEFINE_PAIR(nth, values[i], lily_container_val *source, int i)

DEFINE_SETTERS(return, call_chain->return_target, lily_vm_state *source)

uint32_t lily_container_num_values(lily_container_val *cv)
{
    return cv->num_values;
}

#define TYPE_FN(name, PRE, INPUT, POST, return_type, ...) \
return_type lily_##name##_boolean(__VA_ARGS__, int v) \
{ PRE; lily_move_boolean(INPUT, v); POST; } \
return_type lily_##name##_byte(__VA_ARGS__, uint8_t v) \
{ PRE; lily_move_byte(INPUT, v); POST; } \
return_type lily_##name##_bytestring(__VA_ARGS__, lily_bytestring_val * v) \
{ PRE; lily_move_bytestring(INPUT, v); POST; } \
return_type lily_##name##_double(__VA_ARGS__, double v) \
{ PRE; lily_move_double(INPUT, v); POST; } \
return_type lily_##name##_empty_variant(__VA_ARGS__, uint16_t f) \
{ PRE; lily_move_empty_variant(f, INPUT); POST; } \
return_type lily_##name##_file(__VA_ARGS__, lily_file_val * v) \
{ PRE; lily_move_file(INPUT, v); POST; } \
return_type lily_##name##_foreign(__VA_ARGS__, lily_foreign_val * v) \
{ PRE; lily_move_foreign_f(MOVE_DEREF_SPECULATIVE, INPUT, v); POST; } \
return_type lily_##name##_hash(__VA_ARGS__, lily_hash_val * v) \
{ PRE; lily_move_hash_f(MOVE_DEREF_SPECULATIVE, INPUT, v); POST; } \
return_type lily_##name##_instance(__VA_ARGS__, lily_container_val * v) \
{ PRE; lily_move_instance_f(MOVE_DEREF_SPECULATIVE, INPUT, v); POST; } \
return_type lily_##name##_integer(__VA_ARGS__, int64_t v) \
{ PRE; lily_move_integer(INPUT, v); POST; } \
return_type lily_##name##_list(__VA_ARGS__, lily_container_val * v) \
{ PRE; lily_move_list_f(MOVE_DEREF_SPECULATIVE, INPUT, v); POST; } \
return_type lily_##name##_string(__VA_ARGS__, lily_string_val * v) \
{ PRE; lily_move_string(INPUT, v); POST; } \
return_type lily_##name##_tuple(__VA_ARGS__, lily_container_val * v) \
{ PRE; lily_move_tuple_f(MOVE_DEREF_SPECULATIVE, INPUT, v); POST; } \
return_type lily_##name##_unit(__VA_ARGS__) \
{ PRE; lily_move_unit(INPUT); POST; } \
return_type lily_##name##_value(__VA_ARGS__, lily_value * v) \
{ PRE; lily_value_assign(INPUT, v); POST; } \
return_type lily_##name##_variant(__VA_ARGS__, lily_container_val * v) \
{ PRE; lily_move_variant_f(MOVE_DEREF_SPECULATIVE, INPUT, v); POST; } \

TYPE_FN(box, lily_value *r = s->regs_from_main[0], r, return r, lily_value *, lily_state *s)

/* Special-cased returns */

void lily_value_assign_noref(lily_value *, lily_value *);

void lily_return_none(lily_state *s)
{
    lily_move_empty_variant(LILY_NONE_ID, s->call_chain->return_target);
}

void lily_return_value_noref(lily_state *s, lily_value *v)
{
    lily_value_assign_noref(s->call_chain->return_target, v);
}

/* Argument and result operations */

DEFINE_GETTERS(arg, ->call_chain->locals[index], lily_vm_state *source, int index)
DEFINE_GETTERS(value, , lily_value *source)

int lily_arg_count(lily_state *s)
{
    return s->call_chain->regs_used;
}

lily_value *lily_arg_nth_get(lily_state *s, int reg_i, int container_i)
{
    return s->call_chain->locals[reg_i]->value.container->values[container_i];
}

int lily_arg_is_some(lily_state *s, int i)
{
    return s->call_chain->locals[i]->class_id == LILY_SOME_ID;
}

int lily_arg_is_right(lily_state *s, int i)
{
    return s->call_chain->locals[i]->class_id == LILY_RIGHT_ID;
}

int lily_result_boolean(lily_state *s)
{
    return s->call_chain->next->return_target->value.integer;
}

lily_value *lily_result_value(lily_state *s)
{
    return s->call_chain->next->return_target;
}

/* Stack operations
   Push operations are located within the vm, so that stack growing can remain
   internal to the vm. */

lily_value *lily_take_value(lily_state *s)
{
    s->call_chain->total_regs--;
    return s->regs_from_main[s->call_chain->total_regs];
}

void lily_pop_value(lily_state *s)
{
    s->call_chain->total_regs--;
    lily_value *z = s->regs_from_main[s->call_chain->total_regs];
    lily_deref(z);
    z->flags = 0;
}

/* Raw value creation functions. */

lily_container_val *new_container(uint16_t class_id, int num_values)
{
    lily_container_val *cv = lily_malloc(sizeof(lily_container_val));
    cv->values = lily_malloc(num_values * sizeof(lily_value *));
    cv->refcount = 0;
    cv->num_values = num_values;
    cv->extra_space = 0;
    cv->class_id = class_id;
    cv->gc_entry = NULL;

    int i;
    for (i = 0;i < num_values;i++) {
        lily_value *elem = lily_malloc(sizeof(lily_value));
        elem->flags = 0;
        cv->values[i] = elem;
    }

    return cv;
}

lily_container_val *lily_new_dynamic(void)
{
    return new_container(LILY_DYNAMIC_ID, 1);
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

lily_foreign_val *lily_new_foreign(lily_state *s, uint16_t id,
        lily_destroy_func func, size_t size)
{
    lily_foreign_val *fv = lily_malloc(size);
    fv->refcount = 0;
    fv->class_id = id;
    fv->destroy_func = func;

    return fv;
}

lily_container_val *lily_new_list(int num_values)
{
    return new_container(LILY_LIST_ID, num_values);
}

lily_container_val *lily_new_instance(uint16_t class_id, int initial)
{
    return new_container(class_id, initial);
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

lily_container_val *lily_new_tuple(int num_values)
{
    return new_container(LILY_TUPLE_ID, num_values);
}

lily_container_val *lily_new_some(void)
{
    return new_container(LILY_SOME_ID, 1);
}

lily_container_val *lily_new_left(void)
{
    return new_container(LILY_LEFT_ID, 1);
}

lily_container_val *lily_new_right(void)
{
    return new_container(LILY_RIGHT_ID, 1);
}

lily_container_val *lily_new_variant(uint16_t class_id, int num_values)
{
    return new_container(class_id, num_values);
}

/* Simple per-type operations. */

lily_bytestring_val *lily_new_bytestring_sized(const char *source, int len)
{
    return (lily_bytestring_val *)lily_new_string_sized(source, len);
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

/* Operations */

extern void lily_destroy_hash(lily_value *);
extern lily_gc_entry *lily_gc_stopper;

static void destroy_container(lily_value *v)
{
    lily_container_val *iv = v->value.container;
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
    lily_container_val *lv = v->value.container;

    int i;
    for (i = 0;i < lv->num_values;i++) {
        lily_deref(lv->values[i]);
        lily_free(lv->values[i]);
    }

    lily_free(lv->values);
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
    else if (v->flags & VAL_IS_CONTAINER)
        destroy_container(v);
    else if (class_id == LILY_STRING_ID || class_id == LILY_BYTESTRING_ID)
        destroy_string(v);
    else if (class_id == LILY_FUNCTION_ID)
        destroy_function(v);
    else if (class_id == LILY_HASH_ID)
        lily_destroy_hash(v);
    else if (class_id == LILY_FILE_ID)
        destroy_file(v);
    else if (v->flags & VAL_IS_FOREIGN) {
        v->value.foreign->destroy_func(v->value.generic);
        lily_free(v->value.generic);
    }
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
    lily_container_val *left_list = left->value.container;
    lily_container_val *right_list = right->value.container;
    int ok;
    if (left_list->num_values == right_list->num_values) {
        ok = 1;
        int i;
        for (i = 0;i < left_list->num_values;i++) {
            lily_value *left_item = left_list->values[i];
            lily_value *right_item = right_list->values[i];
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
    else if (left->flags & VAL_IS_ENUM) {
        int ok;
        if (left_tag == right_tag) {
            if (left->value.container == NULL)
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

void lily_push_super(lily_state *s, lily_container_val **iv, uint16_t id,
        uint32_t initial)
{
    lily_value *v = s->call_chain->return_target;

    if (v->flags & VAL_IS_INSTANCE) {
        lily_container_val *pending_instance = v->value.container;
        if (pending_instance->instance_ctor_need != 0) {
            pending_instance->instance_ctor_need = 0;
            *iv = pending_instance;
            lily_push_value(s, v);
            return;
        }
    }

    lily_container_val *new_iv = lily_new_instance(id, initial);
    lily_push_instance(s, new_iv);
    *iv = new_iv;
}
