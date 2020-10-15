#include <string.h>

#include "lily.h"
#include "lily_alloc.h"
#include "lily_value_flags.h"
#include "lily_value_raw.h"
#include "lily_value_structs.h"
#include "lily_vm.h"

#define DEFINE_GETTERS(name, action, ...) \
int lily_##name##_boolean(__VA_ARGS__) \
{ return source  action->value.integer; } \
uint8_t lily_##name##_byte(__VA_ARGS__) \
{ return source  action->value.integer; } \
lily_bytestring_val *lily_##name##_bytestring(__VA_ARGS__) \
{ return (lily_bytestring_val *)source  action->value.string; } \
lily_container_val *lily_##name##_container(__VA_ARGS__) \
{ return source  action->value.container; } \
lily_coroutine_val *lily_##name##_coroutine(__VA_ARGS__) \
{ return source  action->value.coroutine; } \
double lily_##name##_double(__VA_ARGS__) \
{ return source  action->value.doubleval; } \
lily_file_val *lily_##name##_file(__VA_ARGS__) \
{ return source  action->value.file; } \
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

DEFINE_GETTERS(arg, ->call_chain->start[index], lily_state *source, int index)
DEFINE_GETTERS(as, , lily_value *source)

/* This one isn't in the getters because it would be silly to make a function
   that just returns the value sent. */
lily_value *lily_arg_value(lily_state *s, int index)
{
    return s->call_chain->start[index];
}

lily_value *lily_con_get(lily_container_val *c, int index)
{
    return c->values[index];
}

void lily_con_set(lily_container_val *c, int index, lily_value *v)
{
    lily_value_assign(c->values[index], v);
}

void lily_con_set_from_stack(lily_state *s, lily_container_val *c, int index)
{
    lily_value *target = c->values[index];

    if (target->flags & VAL_IS_DEREFABLE)
        lily_deref(target);

    s->call_chain->top--;

    lily_value *top = *(s->call_chain->top);
    *target = *top;

    top->flags = 0;
}

uint32_t lily_con_size(lily_container_val *c)
{
    return c->num_values;
}

int lily_arg_count(lily_state *s)
{
    return (int)(s->call_chain->top - s->call_chain->start);
}

int lily_arg_isa(lily_state *s, int index, uint16_t class_id)
{
    lily_value *value = s->call_chain->start[index];
    int base = FLAGS_TO_BASE(value);
    uint16_t result_id;

    if (base == V_VARIANT_BASE || base == V_INSTANCE_BASE ||
        base == V_FOREIGN_BASE || base == V_COROUTINE_BASE)
        result_id = (uint16_t)value->value.container->class_id;
    else if (base == V_EMPTY_VARIANT_BASE)
        result_id = (uint16_t)value->value.integer;
    else if (base == V_UNIT_BASE)
        result_id = LILY_ID_UNIT;
    else
        /* The other bases map directly to class ids. */
        result_id = (uint16_t)base;

    return result_id == class_id;
}

/* Attempt to safely get the raw value of the argument at 'index' as long as it
   has the marker 'base'. If the argument is missing, unset, or has the wrong
   base, then the fallback value is returned. */
static lily_raw_value get_arg_or(lily_state *s, int index, uint32_t base,
        lily_raw_value fallback)
{
    lily_raw_value result = fallback;

    if (lily_arg_count(s) > index) {
        lily_value *v = s->call_chain->start[index];

        if (FLAGS_TO_BASE(v) == base)
            result = v->value;
    }

    return result;
}

int lily_optional_boolean(lily_state *s, int index, int fallback)
{
    lily_raw_value fake = {.integer = fallback};
    lily_raw_value raw = get_arg_or(s, index, V_BOOLEAN_BASE, fake);

    return (int)raw.integer;
}

int64_t lily_optional_integer(lily_state *s, int index, int64_t fallback)
{
    lily_raw_value fake = {.integer = fallback};
    lily_raw_value raw = get_arg_or(s, index, V_INTEGER_BASE, fake);

    return raw.integer;
}

const char *lily_optional_string_raw(lily_state *s, int index,
        const char *fallback)
{
    lily_string_val sv = {.string = (char *)fallback, .size = 0};
    lily_raw_value fake = {.string = &sv};
    lily_raw_value raw = get_arg_or(s, index, V_STRING_BASE, fake);

    return raw.string->string;
}

lily_value_group lily_value_get_group(lily_value *value)
{
    lily_value_group result = lily_isa_unit;

    switch (FLAGS_TO_BASE(value)) {
        case V_INTEGER_BASE:
            result = lily_isa_integer;
            break;
        case V_DOUBLE_BASE:
            result = lily_isa_double;
            break;
        case V_STRING_BASE:
            result = lily_isa_string;
            break;
        case V_BYTE_BASE:
            result = lily_isa_byte;
            break;
        case V_BYTESTRING_BASE:
            result = lily_isa_bytestring;
            break;
        case V_BOOLEAN_BASE:
            result = lily_isa_boolean;
            break;
        case V_FUNCTION_BASE:
            result = lily_isa_function;
            break;
        case V_LIST_BASE:
            result = lily_isa_list;
            break;
        case V_HASH_BASE:
            result = lily_isa_hash;
            break;
        case V_TUPLE_BASE:
            result = lily_isa_tuple;
            break;
        case V_FILE_BASE:
            result = lily_isa_file;
            break;
        case V_COROUTINE_BASE:
            result = lily_isa_coroutine;
            break;
        case V_FOREIGN_BASE:
            result = lily_isa_foreign_class;
            break;
        case V_INSTANCE_BASE:
            result = lily_isa_native_class;
            break;
        case V_UNIT_BASE:
            result = lily_isa_unit;
            break;
        case V_VARIANT_BASE:
            result = lily_isa_variant;
            break;
        case V_EMPTY_VARIANT_BASE:
            result = lily_isa_empty_variant;
            break;
    }

    return result;
}

/* Stack operations
   Push operations are located within the vm, so that stack growing can remain
   internal to the vm. */

lily_value *lily_stack_take(lily_state *s)
{
    s->call_chain->top--;
    return *s->call_chain->top;
}

void lily_stack_push_and_destroy(lily_state *s, lily_value *v)
{
    lily_push_value(s, v);
    lily_deref(v);
    lily_free(v);
}

lily_value *lily_stack_get_top(lily_state *s)
{
    return *(s->call_chain->top - 1);
}

void lily_stack_drop_top(lily_state *s)
{
    s->call_chain->top--;
    lily_value *z = *s->call_chain->top;
    lily_deref(z);
    z->flags = 0;
}

/* Simple per-type operations. */

void lily_list_take(lily_state *s, lily_container_val *c, uint32_t index)
{
    lily_value *v = c->values[index];
    lily_push_value(s, v);

    lily_deref(v);
    lily_free(v);

    if (index != c->num_values)
        memmove(c->values + index, c->values + index + 1,
                (c->num_values - index - 1) * sizeof(*c->values));

    c->num_values--;
    c->extra_space++;
}

static void grow_list(lily_container_val *lv)
{
    /* There's probably room for improvement here, later on. */
    uint32_t extra = (lv->num_values + 8) >> 2;
    lv->values = lily_realloc(lv->values,
            (lv->num_values + extra) * sizeof(*lv->values));
    lv->extra_space = extra;
}

void lily_list_reserve(lily_container_val *c, uint32_t new_size)
{
    uint32_t size = c->num_values + c->extra_space;

    if (size > new_size)
        return;

    if (size == 0)
        size = 8;

    while (size < new_size)
        size *= 2;

    c->values = lily_realloc(c->values, size * sizeof(*c->values));
    c->extra_space = size - c->num_values;
}

void lily_list_push(lily_container_val *c, lily_value *v)
{
    if (c->extra_space == 0)
        grow_list(c);

    c->values[c->num_values] = lily_value_copy(v);
    c->num_values++;
    c->extra_space--;
}

void lily_list_insert(lily_container_val *c, uint32_t index, lily_value *v)
{
    if (c->extra_space == 0)
        grow_list(c);

    if (index != c->num_values)
        memmove(c->values + index + 1, c->values + index,
                (c->num_values - index) * sizeof(*c->values));

    c->values[index] = lily_value_copy(v);
    c->num_values++;
    c->extra_space--;
}

char *lily_bytestring_raw(lily_bytestring_val *sv)
{
    return sv->string;
}

int lily_bytestring_length(lily_bytestring_val *sv)
{
    return sv->size;
}

FILE *lily_file_for_write(lily_state *s, lily_file_val *filev)
{
    if (filev->inner_file == NULL)
        lily_IOError(s, "IO operation on closed file.");

    if (filev->write_ok == 0)
        lily_IOError(s, "File not open for writing.");

    return filev->inner_file;
}

FILE *lily_file_for_read(lily_state *s, lily_file_val *filev)
{
    if (filev->inner_file == NULL)
        lily_IOError(s, "IO operation on closed file.");

    if (filev->read_ok == 0)
        lily_IOError(s, "File not open for reading.");

    return filev->inner_file;
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

    uint32_t i;
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

    uint32_t i;
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

static void destroy_file(lily_value *v)
{
    lily_file_val *filev = v->value.file;

    if (filev->inner_file && filev->is_builtin == 0)
        fclose(filev->inner_file);

    lily_free(filev);
}

void lily_destroy_vm(lily_vm_state *);

static void destroy_coroutine(lily_value *v)
{
    lily_coroutine_val *co_val = v->value.coroutine;
    if (co_val->gc_entry == lily_gc_stopper)
        return;

    int full_destroy = 1;

    /* There's no need to check for a gc entry like with other values.
       Coroutines always have a gc tag. */

    if (co_val->gc_entry->last_pass == -1) {
        full_destroy = 0;
        co_val->gc_entry = lily_gc_stopper;
    }
    else
        co_val->gc_entry->value.generic = NULL;

    lily_value *receiver = co_val->receiver;

    /* The receiver can reference values within the vm's inner registers, so it
       has to be dropped first. */
    if (receiver->flags & VAL_IS_DEREFABLE)
        lily_deref(receiver);

    lily_vm_state *base_vm = co_val->vm;

    /* The vm normally doesn't drop the raiser because parser does it.
       Coroutines have to do it themselves. */
    lily_free_raiser(base_vm->raiser);

    /* Do a direct destroy of the base frame because the Coroutine has the only
       reference to it (it's never put into a register). */
    lily_value func_v;
    func_v.flags = V_FUNCTION_BASE;
    func_v.value.function = co_val->base_function;

    destroy_function(&func_v);

    lily_destroy_vm(base_vm);
    lily_free(base_vm);
    lily_free(receiver);

    if (full_destroy)
        lily_free(co_val);
}

void lily_value_destroy(lily_value *v)
{
    int base = FLAGS_TO_BASE(v);

    if (base == V_LIST_BASE || base == V_TUPLE_BASE)
        destroy_list(v);
    else if (base == V_VARIANT_BASE || base == V_INSTANCE_BASE)
        destroy_container(v);
    else if (base == V_STRING_BASE || base == V_BYTESTRING_BASE)
        destroy_string(v);
    else if (base == V_FUNCTION_BASE)
        destroy_function(v);
    else if (base == V_HASH_BASE)
        lily_destroy_hash(v);
    else if (base == V_FILE_BASE)
        destroy_file(v);
    else if (base == V_COROUTINE_BASE)
        destroy_coroutine(v);
    else if (base == V_FOREIGN_BASE) {
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

/* Create a copy of a value. It may get a ref. */
lily_value *lily_value_copy(lily_value *input)
{
    if (input->flags & VAL_IS_DEREFABLE)
        input->value.generic->refcount++;

    lily_value *result = lily_malloc(sizeof(*result));
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
        uint32_t i;
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
    int left_base = FLAGS_TO_BASE(left);
    int right_base = FLAGS_TO_BASE(right);

    if (*depth == 100)
        lily_RuntimeError(s, "Infinite loop in comparison.");

    if (left_base != right_base)
        return 0;
    else if (left_base == V_INTEGER_BASE || left_base == V_BOOLEAN_BASE)
        return left->value.integer == right->value.integer;
    else if (left_base == V_DOUBLE_BASE)
        return left->value.doubleval == right->value.doubleval;
    else if (left_base == V_STRING_BASE)
        return strcmp(left->value.string->string,
                right->value.string->string) == 0;
    else if (left_base == V_BYTESTRING_BASE) {
        lily_string_val *left_sv = left->value.string;
        lily_string_val *right_sv = right->value.string;
        char *left_s = left_sv->string;
        char *right_s = right_sv->string;
        uint32_t left_size = left_sv->size;
        return (left_size == right_sv->size &&
                memcmp(left_s, right_s, left_size) == 0);
    }
    else if (left_base == V_LIST_BASE || left_base == V_TUPLE_BASE) {
        return subvalue_eq(s, depth, left, right);
    }
    else if (left_base == V_HASH_BASE) {
        lily_hash_val *left_hash = left->value.hash;
        lily_hash_val *right_hash = right->value.hash;

        int ok = 1;
        if (left_hash->num_entries != right_hash->num_entries)
            ok = 0;

        if (ok) {
            (*depth)++;
            int i;
            for (i = 0;i < left_hash->num_bins;i++) {
                lily_hash_entry *left_bin = left_hash->bins[i];
                if (left_bin) {
                    lily_value *right_value = lily_hash_get(s, right_hash,
                            left_bin->boxed_key);

                    if (right_value == NULL ||
                        lily_value_compare_raw(s, depth, left_bin->record,
                                right_value) == 0) {
                        ok = 0;
                        break;
                    }
                }
            }
            (*depth)--;
        }
        return ok;
    }
    else if (left_base == V_VARIANT_BASE) {
        int ok;
        if (left->value.container->class_id ==
            right->value.container->class_id)
            ok = subvalue_eq(s, depth, left, right);
        else
            ok = 0;

        return ok;
    }
    else if (left_base == V_EMPTY_VARIANT_BASE)
        /* Empty variants store their class id here. */
        return left->value.integer == right->value.integer;
    else
        /* Everything else gets pointer equality. */
        return left->value.generic == right->value.generic;
}

int lily_value_compare(lily_state *s, lily_value *left, lily_value *right)
{
    int depth = 0;
    return lily_value_compare_raw(s, &depth, left, right);
}

uint16_t lily_cid_at(lily_vm_state *vm, int n)
{
    return vm->call_chain->function->cid_table[n];
}
