#include <string.h>

#include "lily.h"
#include "lily_alloc.h"
#include "lily_value.h"
#include "lily_vm.h"

/* This file contains implementations for most of the functions in lily.h, and
   all of the functions defined in lily_value.h.
   Functions inside of lily.h that are not contained in this file are absent
   because they fit better elsewhere (ex: hashes), or have their own file since
   they are specialized (ex: msgbuf).
   Functions from lily_value.h come before those in lily.h. Definitions in this
   file should have the same order as the matching header files. */

extern lily_gc_entry *lily_gc_stopper;

void lily_destroy_hash(lily_value *);
void lily_destroy_vm(lily_vm_state *);
void lily_vm_grow_registers(lily_vm_state *, uint16_t);


/* Raw value creation. */


static lily_string_val *new_sv(char *buffer, int size)
{
    lily_string_val *sv = lily_malloc(sizeof(*sv));

    sv->refcount = 1;
    sv->string = buffer;
    sv->size = size;
    return sv;
}

lily_bytestring_val *lily_new_bytestring_raw(const char *source, int len)
{
    char *buffer = lily_malloc((len + 1) * sizeof(*buffer));

    memcpy(buffer, source, len);
    buffer[len] = '\0';
    return (lily_bytestring_val *)new_sv(buffer, len);
}

lily_string_val *lily_new_string_raw(const char *source)
{
    size_t len = strlen(source);
    char *buffer = lily_malloc((len + 1) * sizeof(*buffer));

    strcpy(buffer, source);

    return new_sv(buffer, (int)len);
}

lily_container_val *lily_new_container_raw(uint16_t class_id,
        uint32_t num_values)
{
    lily_container_val *cv = lily_malloc(sizeof(*cv));

    cv->values = lily_malloc(num_values * sizeof(*cv->values));
    cv->refcount = 1;
    cv->num_values = num_values;
    cv->extra_space = 0;
    cv->class_id = class_id;
    cv->gc_entry = NULL;

    uint32_t i;

    for (i = 0;i < num_values;i++) {
        lily_value *elem = lily_malloc(sizeof(*elem));

        elem->flags = 0;
        cv->values[i] = elem;
    }

    return cv;
}


/* Internal api (not in lily.h) functions for handling values. */


void lily_deref(lily_value *value)
{
    if (value->flags & VAL_IS_DEREFABLE) {
        value->value.generic->refcount--;
        if (value->value.generic->refcount == 0)
            lily_value_destroy(value);
    }
}

void lily_stack_push_and_destroy(lily_state *s, lily_value *v)
{
    lily_push_value(s, v);
    lily_deref(v);
    lily_free(v);
}

lily_value *lily_stack_take(lily_state *s)
{
    s->call_chain->top--;
    return *s->call_chain->top;
}

void lily_value_assign(lily_value *left, lily_value *right)
{
    if (right->flags & VAL_IS_DEREFABLE)
        right->value.generic->refcount++;

    if (left->flags & VAL_IS_DEREFABLE)
        lily_deref(left);

    left->value = right->value;
    left->flags = right->flags;
}

uint16_t lily_value_class_id(lily_value *value)
{
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

    return result_id;
}

static int lily_value_compare_raw(lily_vm_state *, int *, lily_value *,
        lily_value *);

static int subvalue_eq(lily_state *s, int *depth, lily_value *left,
        lily_value *right)
{
    lily_container_val *left_list = left->value.container;
    lily_container_val *right_list = right->value.container;
    int ok;
    uint32_t i;

    if (left_list->num_values == right_list->num_values) {
        ok = 1;
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

int lily_value_compare_raw(lily_state *s, int *depth, lily_value *left,
        lily_value *right)
{
    int left_base = FLAGS_TO_BASE(left);

    if (*depth == 100)
        lily_RuntimeError(s, "Infinite loop in comparison.");

    if (left_base == V_INTEGER_BASE || left_base == V_BOOLEAN_BASE)
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
    else if (left_base == V_LIST_BASE || left_base == V_TUPLE_BASE)
        return subvalue_eq(s, depth, left, right);
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

                while (left_bin) {
                    lily_value *right_value = lily_hash_get(s, right_hash,
                            left_bin->boxed_key);

                    if (right_value == NULL ||
                        lily_value_compare_raw(s, depth, left_bin->record,
                                right_value) == 0) {
                        ok = 0;
                        break;
                    }

                    left_bin = left_bin->next;
                }
            }
            (*depth)--;
        }
        return ok;
    }
    else if (left_base == V_VARIANT_BASE) {
        int ok;
        if (FLAGS_TO_BASE(right) == V_VARIANT_BASE &&
            left->value.container->class_id ==
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

lily_value *lily_value_copy(lily_value *input)
{
    if (input->flags & VAL_IS_DEREFABLE)
        input->value.generic->refcount++;

    lily_value *result = lily_malloc(sizeof(*result));

    result->flags = input->flags;
    result->value = input->value;
    return result;
}

static void destroy_container(lily_value *v)
{
    lily_container_val *iv = v->value.container;

    if (iv->gc_entry == lily_gc_stopper)
        return;

    int full_destroy = 1;

    if (iv->gc_entry) {
        if (iv->gc_entry->status == GC_SWEEP) {
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

static void destroy_function(lily_value *);

static void destroy_coroutine(lily_value *v)
{
    lily_coroutine_val *co_val = v->value.coroutine;

    if (co_val->gc_entry == lily_gc_stopper)
        return;

    int full_destroy = 1;

    /* There's no need to check for a gc entry like with other values.
       Coroutines always have a gc tag. */

    if (co_val->gc_entry->status == GC_SWEEP) {
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

    /* Finish off by dropping the Coroutine's vm. */
    lily_destroy_vm(base_vm);
    lily_free(base_vm);
    lily_free(receiver);

    if (full_destroy)
        lily_free(co_val);
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

static void destroy_file(lily_value *v)
{
    lily_file_val *filev = v->value.file;

    if (filev->close_func)
        filev->close_func(filev->inner_file);

    lily_free(filev);
}

static void destroy_function(lily_value *v)
{
    lily_function_val *fv = v->value.function;

    if (fv->gc_entry == lily_gc_stopper)
        return;

    int full_destroy = 1;

    if (fv->gc_entry) {
        if (fv->gc_entry->status == GC_SWEEP) {
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

static void destroy_string(lily_value *v)
{
    lily_string_val *sv = v->value.string;

    lily_free(sv->string);
    lily_free(sv);
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


/* Raw value operations. */


char *lily_bytestring_raw(lily_bytestring_val *sv)
{
    return sv->string;
}

uint32_t lily_bytestring_length(lily_bytestring_val *sv)
{
    return sv->size;
}

lily_value *lily_con_get(lily_container_val *c, uint32_t index)
{
    return c->values[index];
}

void lily_con_set(lily_container_val *c, uint32_t index, lily_value *v)
{
    lily_value_assign(c->values[index], v);
}

void lily_con_set_from_stack(lily_state *s, lily_container_val *c,
        uint32_t index)
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

FILE *lily_file_for_write(lily_state *s, lily_file_val *filev)
{
    if (filev->close_func == NULL)
        lily_IOError(s, "IO operation on closed file.");

    if (filev->write_ok == 0)
        lily_IOError(s, "File not open for writing.");

    return filev->inner_file;
}

FILE *lily_file_for_read(lily_state *s, lily_file_val *filev)
{
    if (filev->close_func == NULL)
        lily_IOError(s, "IO operation on closed file.");

    if (filev->read_ok == 0)
        lily_IOError(s, "File not open for reading.");

    return filev->inner_file;
}

uint16_t *lily_function_bytecode(lily_function_val *fv, uint16_t *len)
{
    uint16_t *result;

    if (fv->code) {
        *len = fv->code_len;
        result = fv->code;
    }
    else {
        *len = 0;
        result = NULL;
    }

    return result;
}

int lily_function_is_foreign(lily_function_val *fv)
{
    return fv->code == NULL;
}

int lily_function_is_native(lily_function_val *fv)
{
    return fv->code != NULL;
}

static void grow_list(lily_container_val *lv)
{
    /* There's probably room for improvement here, later on. */
    uint32_t extra = (lv->num_values + 8) >> 2;
    lv->values = lily_realloc(lv->values,
            (lv->num_values + extra) * sizeof(*lv->values));
    lv->extra_space = extra;
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

char *lily_string_raw(lily_string_val *sv)
{
    return sv->string;
}

uint32_t lily_string_length(lily_string_val *sv)
{
    return sv->size;
}


/* Argument handling functions. */


int lily_arg_boolean(lily_state *s, int index)
{
    return (int)s->call_chain->start[index]->value.integer;
}

uint8_t lily_arg_byte(lily_state *s, int index)
{
    return (uint8_t)s->call_chain->start[index]->value.integer;
}

lily_bytestring_val *lily_arg_bytestring(lily_state *s, int index)
{
    return (lily_bytestring_val *)s->call_chain->start[index]->value.string;
}

lily_container_val *lily_arg_container(lily_state *s, int index)
{
    return s->call_chain->start[index]->value.container;
}

double lily_arg_double(lily_state *s, int index)
{
    return s->call_chain->start[index]->value.doubleval;
}

lily_file_val *lily_arg_file(lily_state *s, int index)
{
    return s->call_chain->start[index]->value.file;
}

lily_function_val *lily_arg_function(lily_state *s, int index)
{
    return s->call_chain->start[index]->value.function;
}

lily_hash_val *lily_arg_hash(lily_state *s, int index)
{
    return s->call_chain->start[index]->value.hash;
}

lily_generic_val *lily_arg_generic(lily_state *s, int index)
{
    return s->call_chain->start[index]->value.generic;
}

int64_t lily_arg_integer(lily_state *s, int index)
{
    return s->call_chain->start[index]->value.integer;
}

lily_string_val *lily_arg_string(lily_state *s, int index)
{
    return s->call_chain->start[index]->value.string;
}

char *lily_arg_string_raw(lily_state *s, int index)
{
    return s->call_chain->start[index]->value.string->string;
}

lily_value *lily_arg_value(lily_state *s, int index)
{
    return s->call_chain->start[index];
}

uint16_t lily_arg_count(lily_state *s)
{
    return (uint16_t)(s->call_chain->top - s->call_chain->start);
}

int lily_arg_isa(lily_state *s, int index, uint16_t class_id)
{
    lily_value *v = s->call_chain->start[index];
    uint16_t result_id = lily_value_class_id(v);

    return result_id == class_id;
}


/* Functions for easily implementing optional values.
   Simple optional arguments can be handled by using `lily_arg_count`. These
   functions are provided for convenience, and because they account for both
   optargs (too few) and keyargs (unset values). */


/* Attempt to safely get the raw value of the argument at 'index' as long as it
   has the marker 'base'. If the argument is missing, unset, or has the wrong
   base, then the fallback value is returned. */
static lily_value *maybe_get_value(lily_state *s, int index, uint32_t base)
{
    lily_value *result = NULL;

    if (lily_arg_count(s) > index) {
        lily_value *v = s->call_chain->start[index];

        if (FLAGS_TO_BASE(v) == base)
            result = v;
    }

    return result;
}

int lily_optional_boolean(lily_state *s, int index, int fallback)
{
    int result = fallback;
    lily_value *v = maybe_get_value(s, index, V_BOOLEAN_BASE);

    if (v)
        result = (int)v->value.integer;

    return result;
}

int64_t lily_optional_integer(lily_state *s, int index, int64_t fallback)
{
    int64_t result = fallback;
    lily_value *v = maybe_get_value(s, index, V_INTEGER_BASE);

    if (v)
        result = v->value.integer;

    return result;
}

const char *lily_optional_string_raw(lily_state *s, int index,
        const char *fallback)
{
    const char *result = fallback;
    lily_value *v = maybe_get_value(s, index, V_STRING_BASE);

    if (v)
        result = v->value.string->string;

    return result;
}


/* Value pushing functions. */


/* This sets gc speculative on the value in case it happens to hold a tagged
   value. The gc will walk values that it shouldn't, but better that than to
   miss a tagged value. It's a small price to pay since marking values as
   speculative doesn't count toward the tag threshold that invokes the gc. */
#define PUSH_CONTAINER(id, container_flags, size) \
PUSH_PREAMBLE \
lily_container_val *c = lily_new_container_raw(id, size); \
SET_TARGET(VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE | container_flags, container, c); \
return c

#define PUSH_PREAMBLE \
lily_call_frame *frame = s->call_chain; \
if (frame->top == frame->register_end) { \
    lily_vm_grow_registers(s, 1); \
} \
 \
lily_value *target = *frame->top; \
if (target->flags & VAL_IS_DEREFABLE) \
    lily_deref(target); \
 \
frame->top++;

#define SET_TARGET(push_flags, field, push_value) \
target->flags = push_flags; \
target->value.field = push_value

/* This isn't part of the api, but it's here because it's a push function. */
void lily_push_coroutine(lily_state *s, lily_coroutine_val *co)
{
    /* The caller will tag the value, so don't set a speculative flag. */
    PUSH_PREAMBLE
    SET_TARGET(V_COROUTINE_BASE | VAL_IS_DEREFABLE, coroutine, co);
}

void lily_push_boolean(lily_state *s, int v)
{
    PUSH_PREAMBLE
    SET_TARGET(V_BOOLEAN_BASE | V_BOOLEAN_FLAG, integer, v);
}

void lily_push_bytestring(lily_state *s, const char *source, int len)
{
    PUSH_PREAMBLE
    char *buffer = lily_malloc((len + 1) * sizeof(*buffer));

    memcpy(buffer, source, len);
    buffer[len] = '\0';

    lily_string_val *sv = new_sv(buffer, len);

    SET_TARGET(V_BYTESTRING_FLAG | V_BYTESTRING_BASE | VAL_IS_DEREFABLE, string, sv);
}

void lily_push_byte(lily_state *s, uint8_t v)
{
    PUSH_PREAMBLE
    SET_TARGET(V_BYTE_FLAG | V_BYTE_BASE, integer, v);
}

void lily_push_double(lily_state *s, double v)
{
    PUSH_PREAMBLE
    SET_TARGET(V_DOUBLE_FLAG | V_DOUBLE_BASE, doubleval, v);
}

void lily_push_empty_variant(lily_state *s, uint16_t id)
{
    PUSH_PREAMBLE
    SET_TARGET(V_EMPTY_VARIANT_BASE, integer, id);
}

void lily_push_file(lily_state *s, FILE *inner_file, const char *mode,
        lily_file_close_func close_func)
{
    PUSH_PREAMBLE
    lily_file_val *filev = lily_malloc(sizeof(*filev));
    int plus = strchr(mode, '+') != NULL;

    filev->refcount = 1;
    filev->inner_file = inner_file;
    filev->read_ok = (*mode == 'r' || plus);
    filev->write_ok = (*mode == 'w' || *mode == 'a' || plus);
    filev->close_func = close_func;
    SET_TARGET(V_FILE_BASE | VAL_IS_DEREFABLE, file, filev);
}

lily_foreign_val *lily_push_foreign(lily_state *s, uint16_t id,
        lily_destroy_func func, size_t size)
{
    PUSH_PREAMBLE
    lily_foreign_val *fv = lily_malloc(size * sizeof(*fv));

    fv->refcount = 1;
    fv->class_id = id;
    fv->destroy_func = func;
    SET_TARGET(VAL_IS_DEREFABLE | V_FOREIGN_BASE, foreign, fv);
    return fv;
}

lily_hash_val *lily_push_hash(lily_state *s, int size)
{
    PUSH_PREAMBLE
    lily_hash_val *h = lily_new_hash_raw(size);

    SET_TARGET(V_HASH_BASE | VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE, hash, h);
    return h;
}

lily_container_val *lily_push_instance(lily_state *s, uint16_t id,
        uint32_t size)
{
    PUSH_CONTAINER(id, V_INSTANCE_BASE, size);
}

void lily_push_integer(lily_state *s, int64_t v)
{
    PUSH_PREAMBLE
    SET_TARGET(V_INTEGER_FLAG | V_INTEGER_BASE, integer, v);
}

lily_container_val *lily_push_list(lily_state *s, uint32_t size)
{
    PUSH_CONTAINER(LILY_ID_LIST, V_LIST_BASE, size);
}

lily_container_val *lily_push_super(lily_state *s, uint16_t id,
        uint32_t initial)
{
    lily_value *v = s->call_chain->return_target;

    if (FLAGS_TO_BASE(v) == V_INSTANCE_BASE) {
        lily_container_val *pending_instance = v->value.container;
        if (pending_instance->instance_ctor_need != 0) {
            pending_instance->instance_ctor_need = 0;
            lily_push_value(s, v);
            return pending_instance;
        }
    }

    /* Create a new instance of the foreign-based native class. This native
       class is considered completely constructed. In the case of foreign-based
       native classes with inheritance (Exception and friends), the caller must
       run the relevant inits.
       This is not an issue with true native classes that inherit foreign-based
       native classes, because they'll be trapped by the above. */
    lily_container_val *cv = lily_push_instance(s, id, initial);
    cv->instance_ctor_need = 0;
    return cv;
}

void lily_push_string(lily_state *s, const char *source)
{
    PUSH_PREAMBLE
    size_t len = strlen(source);
    char *buffer = lily_malloc((len + 1) * sizeof(*buffer));

    strcpy(buffer, source);

    lily_string_val *sv = new_sv(buffer, (int)len);

    SET_TARGET(V_STRING_FLAG | V_STRING_BASE | VAL_IS_DEREFABLE, string, sv);
}

void lily_push_string_sized(lily_state *s, const char *source, int len)
{
    PUSH_PREAMBLE
    char *buffer = lily_malloc((len + 1) * sizeof(*buffer));
    memcpy(buffer, source, len);
    buffer[len] = '\0';

    lily_string_val *sv = new_sv(buffer, len);

    SET_TARGET(V_STRING_FLAG | V_STRING_BASE | VAL_IS_DEREFABLE, string, sv);
}

lily_container_val *lily_push_tuple(lily_state *s, uint32_t size)
{
    PUSH_CONTAINER(LILY_ID_TUPLE, V_TUPLE_BASE, size);
}

void lily_push_unit(lily_state *s)
{
    PUSH_PREAMBLE
    SET_TARGET(V_UNIT_BASE, integer, 0);
}

void lily_push_unset(lily_state *s)
{
    PUSH_PREAMBLE
    SET_TARGET(V_UNSET_BASE, integer, 0);
}

void lily_push_value(lily_state *s, lily_value *v)
{
    PUSH_PREAMBLE
    if (v->flags & VAL_IS_DEREFABLE)
        v->value.generic->refcount++;

    target->flags = v->flags;
    target->value = v->value;
}

lily_container_val *lily_push_variant(lily_state *s, uint16_t id, uint32_t size)
{
    PUSH_CONTAINER(id, V_VARIANT_BASE, size);
}


/* Return a value. */


#define RETURN_PREAMBLE \
lily_value *target = s->call_chain->return_target; \
if (target->flags & VAL_IS_DEREFABLE) \
    lily_deref(target);

void lily_return_boolean(lily_state *s, int v)
{
    RETURN_PREAMBLE
    SET_TARGET(V_BOOLEAN_BASE | V_BOOLEAN_FLAG, integer, v);
}

void lily_return_byte(lily_state *s, uint8_t v)
{
    RETURN_PREAMBLE
    SET_TARGET(V_BYTE_FLAG | V_BYTE_BASE, integer, v);
}

void lily_return_double(lily_state *s, double v)
{
    RETURN_PREAMBLE
    SET_TARGET(V_DOUBLE_FLAG | V_DOUBLE_BASE, doubleval, v);
}

void lily_return_integer(lily_state *s, int64_t v)
{
    RETURN_PREAMBLE
    SET_TARGET(V_INTEGER_FLAG | V_INTEGER_BASE, integer, v);
}

void lily_return_none(lily_state *s)
{
    RETURN_PREAMBLE
    SET_TARGET(V_EMPTY_VARIANT_BASE, integer, LILY_ID_NONE);
}

void lily_return_some_of_top(lily_state *s)
{
    lily_value *target = s->call_chain->return_target;

    if (target->flags & VAL_IS_DEREFABLE)
        lily_deref(target);

    lily_container_val *variant = lily_new_container_raw(LILY_ID_SOME, 1);
    lily_value *entry = variant->values[0];
    lily_value *top = *(s->call_chain->top - 1);

    /* Transfer top into the floating variant. */
    *entry = *top;
    top->flags = 0;

    /* Floating variant slides into the return. */
    SET_TARGET(VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE | V_VARIANT_BASE,
               container, variant);
}

void lily_return_string(lily_state *s, const char *value)
{
    RETURN_PREAMBLE
    size_t len = strlen(value);
    char *buffer = lily_malloc((len + 1) * sizeof(*buffer));

    strcpy(buffer, value);

    lily_string_val *sv = new_sv(buffer, (int)len);

    SET_TARGET(V_STRING_FLAG | V_STRING_BASE | VAL_IS_DEREFABLE, string, sv);
}

void lily_return_super(lily_state *s)
{
    lily_value *target = s->call_chain->return_target;
    lily_value *top = *(s->call_chain->top - 1);

    if (FLAGS_TO_BASE(target) == V_INSTANCE_BASE &&
        target->value.container == top->value.container) {
        return;
    }

    if (target->flags & VAL_IS_DEREFABLE)
        lily_deref(target);

    *target = *top;
    top->flags = 0;
}

void lily_return_top(lily_state *s)
{
    lily_value *target = s->call_chain->return_target;

    if (target->flags & VAL_IS_DEREFABLE)
        lily_deref(target);

    lily_value *top = *(s->call_chain->top - 1);

    *target = *top;
    top->flags = 0;
}

void lily_return_unit(lily_state *s)
{
    RETURN_PREAMBLE
    SET_TARGET(V_UNIT_BASE, container, NULL);
}

void lily_return_value(lily_state *s, lily_value *v)
{
    lily_value *target = s->call_chain->return_target;

    lily_value_assign(target, v);
}


/* Access a value. */


lily_value_group lily_value_get_group(lily_value *value)
{
    lily_value_group result = lily_isa_unit;

    switch (FLAGS_TO_BASE(value)) {
        case V_BOOLEAN_BASE:
            result = lily_isa_boolean;
            break;
        case V_BYTE_BASE:
            result = lily_isa_byte;
            break;
        case V_BYTESTRING_BASE:
            result = lily_isa_bytestring;
            break;
        case V_COROUTINE_BASE:
        case V_FOREIGN_BASE:
            result = lily_isa_foreign_class;
            break;
        case V_DOUBLE_BASE:
            result = lily_isa_double;
            break;
        case V_EMPTY_VARIANT_BASE:
            result = lily_isa_empty_variant;
            break;
        case V_FILE_BASE:
            result = lily_isa_file;
            break;
        case V_FUNCTION_BASE:
            result = lily_isa_function;
            break;
        case V_HASH_BASE:
            result = lily_isa_hash;
            break;
        case V_INSTANCE_BASE:
            result = lily_isa_native_class;
            break;
        case V_INTEGER_BASE:
            result = lily_isa_integer;
            break;
        case V_LIST_BASE:
            result = lily_isa_list;
            break;
        case V_STRING_BASE:
            result = lily_isa_string;
            break;
        case V_TUPLE_BASE:
            result = lily_isa_tuple;
            break;
        case V_UNIT_BASE:
            result = lily_isa_unit;
            break;
        case V_VARIANT_BASE:
            result = lily_isa_variant;
            break;
    }

    return result;
}

int lily_as_boolean(lily_value *v)
{
    return (int)v->value.integer;
}

uint8_t lily_as_byte(lily_value *v)
{
    return (uint8_t)v->value.integer;
}

lily_bytestring_val *lily_as_bytestring(lily_value *v)
{
    return (lily_bytestring_val *)v->value.string;
}

lily_container_val *lily_as_container(lily_value *v)
{
    return v->value.container;
}

double lily_as_double(lily_value *v)
{
    return v->value.doubleval;
}

lily_file_val *lily_as_file(lily_value *v)
{
    return v->value.file;
}

lily_function_val *lily_as_function(lily_value *v)
{
    return v->value.function;
}

lily_hash_val *lily_as_hash(lily_value *v)
{
    return v->value.hash;
}

lily_generic_val *lily_as_generic(lily_value *v)
{
    return v->value.generic;
}

int64_t lily_as_integer(lily_value *v)
{
    return v->value.integer;
}

lily_string_val *lily_as_string(lily_value *v)
{
    return v->value.string;
}

char *lily_as_string_raw(lily_value *v)
{
    return v->value.string->string;
}


/* Stack modification functions. */


void lily_stack_drop_top(lily_state *s)
{
    s->call_chain->top--;

    lily_value *v = *s->call_chain->top;

    lily_deref(v);
    v->flags = 0;
}

lily_value *lily_stack_get_top(lily_state *s)
{
    return *(s->call_chain->top - 1);
}


/* Miscellaneous api. */


uint16_t lily_cid_at(lily_vm_state *vm, int n)
{
    return vm->call_chain->function->cid_table[n];
}

void lily_v21_plus_required(lily_vm_state *vm)
{
    /* This is a hopefully temporary hack for how older Lily interpreters did
       not have a failsafe for unknown dynaload types. This should prevent those
       interpreters from loading modules that will crash them with a hopefully
       obvious error. */
    (void)vm;
}
