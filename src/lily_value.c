#include "lily_alloc.h"
#include "lily_value.h"
#include "lily_vm.h"

/* This is to get their gc collection functions... :( */

#include "lily_cls_list.h"
#include "lily_cls_tuple.h"
#include "lily_cls_hash.h"
#include "lily_cls_any.h"
#include "lily_cls_function.h"

/* Check if the value given is deref-able. If so, hit it with a deref. */
void lily_deref(lily_value *value)
{
    if ((value->flags & VAL_IS_NOT_DEREFABLE) == 0) {
        value->value.generic->refcount--;
        if (value->value.generic->refcount == 0)
            value->type->cls->destroy_func(value);
    }
}

/* This calls deref on the raw part of a value. This should not be used when a
   proper value is available. */
void lily_deref_raw(lily_type *type, lily_raw_value raw)
{
    lily_value v;
    v.type = type;
    v.flags = (type->cls->flags & VAL_IS_PRIMITIVE);
    v.value = raw;

    lily_deref(&v);
}

inline lily_value *lily_new_value(uint64_t flags, lily_type *type,
        lily_raw_value raw)
{
    lily_value *v = lily_malloc(sizeof(lily_value));
    v->flags = flags;
    v->type = type;
    v->value = raw;

    return v;
}

/* Assign one value to another. The right may get a ref, and the left may get a
   deref. Both sides are assumed to be equivalent type-wise (only value and
   flags move over). */
void lily_assign_value(lily_value *left, lily_value *right)
{
    if ((right->flags & VAL_IS_NOT_DEREFABLE) == 0)
        right->value.generic->refcount++;

    if ((left->flags & VAL_IS_NOT_DEREFABLE) == 0)
        lily_deref(left);

    left->value = right->value;
    left->flags = right->flags;
}

/* This puts a raw value into a proper value. The proper value may get a deref,
   but the raw one will not. Use this to put newly-made raw values into a proper
   value. */
void lily_move_raw_value(lily_value *left, lily_raw_value raw_right)
{
    if ((left->flags & VAL_IS_NOT_DEREFABLE) == 0)
        lily_deref(left);

    left->value = raw_right;
    left->flags = (left->type->cls->flags & VAL_IS_PRIMITIVE);
}

/* Create a copy of a value. It may get a ref. */
lily_value *lily_copy_value(lily_value *input)
{
    if ((input->flags & VAL_IS_NOT_DEREFABLE) == 0)
        input->value.generic->refcount++;

    return lily_new_value(input->flags, input->type, input->value);
}

inline lily_instance_val *lily_new_instance_val()
{
    lily_instance_val *ival = lily_malloc(sizeof(lily_instance_val));
    ival->refcount = 1;
    ival->gc_entry = NULL;
    ival->values = NULL;
    ival->num_values = -1;
    ival->visited = 0;
    ival->true_type = NULL;

    return ival;
}

inline lily_instance_val *lily_new_instance_val_for(lily_type *t)
{
    lily_instance_val *ival = lily_new_instance_val();
    int num_values = t->cls->prop_count;
    ival->values = lily_malloc(num_values * sizeof(lily_value *));
    ival->num_values = num_values;
    ival->true_type = t;
    return ival;
}

int lily_generic_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    return (left->value.generic == right->value.generic);
}

int lily_instance_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    int ret;

    if (left->value.instance->true_type == right->value.instance->true_type) {
        int i;
        ret = 1;

        for (i = 0;i < left->value.instance->num_values;i++) {
            lily_value **left_values = left->value.instance->values;
            lily_value **right_values = right->value.instance->values;

            class_eq_func eq_func = left_values[i]->type->cls->eq_func;
            (*depth)++;
            if (eq_func(vm, depth, left_values[i], right_values[i]) == 0) {
                ret = 0;
                (*depth)--;
                break;
            }
            (*depth)--;
        }
    }
    else
        ret = 0;

    return ret;
}

void lily_gc_collect_value(lily_type *value_type,
        lily_raw_value value)
{
    int entry_cls_id = value_type->cls->id;

    if (entry_cls_id == SYM_CLASS_LIST)
        lily_gc_collect_list(value_type, value.list);
    else if (entry_cls_id == SYM_CLASS_HASH)
        lily_gc_collect_hash(value_type, value.hash);
    else if (entry_cls_id == SYM_CLASS_ANY)
        lily_gc_collect_any(value.any);
    else if (entry_cls_id == SYM_CLASS_TUPLE ||
             entry_cls_id >= SYM_CLASS_EXCEPTION)
        lily_gc_collect_tuple(value_type, value.list);
    else if (entry_cls_id == SYM_CLASS_FUNCTION)
        lily_gc_collect_function(value_type, value.function);
    else
        lily_deref_raw(value_type, value);
}
