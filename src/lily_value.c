#include "lily_alloc.h"
#include "lily_value.h"
#include "lily_vm.h"

/* This is to get their gc collection functions... :( */

#include "lily_cls_list.h"
#include "lily_cls_tuple.h"
#include "lily_cls_hash.h"
#include "lily_cls_any.h"
#include "lily_cls_function.h"

/*  lily_deref
    This function will check that the value is refcounted and that it is not
    nil/protected before giving it a deref. It is therefore safe to pass
    anything to this function as long as it's not a NULL value.
    If the value given falls to 0 refs, it is immediately destroyed, as well as
    whatever is inside of it.

    Note: This destroys the contents inside the value, NOT the value itself. */
void lily_deref(lily_value *value)
{
    if ((value->flags & VAL_IS_NOT_DEREFABLE) == 0) {
        value->value.generic->refcount--;
        if (value->value.generic->refcount == 0)
            value->type->cls->destroy_func(value);
    }
}

/*  lily_deref_raw
    This is a helper function for lily_deref. This function calls lily_deref
    with a proper value that has the given type and raw value inside. */
void lily_deref_raw(lily_type *type, lily_raw_value raw)
{
    lily_value v;
    v.type = type;
    v.flags = (type->cls->flags & VAL_IS_PRIMITIVE);
    v.value = raw;

    lily_deref(&v);
}

lily_instance_val *lily_new_instance_val()
{
    lily_instance_val *ival = lily_malloc(sizeof(lily_instance_val));
    ival->refcount = 1;
    ival->gc_entry = NULL;
    ival->values = NULL;
    ival->num_values = -1;
    ival->visited = 0;
    ival->true_class = NULL;

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

    if (left->value.instance->true_class ==
        right->value.instance->true_class) {

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
    else if (value_type->cls->flags & CLS_ENUM_CLASS)
        lily_gc_collect_any(value.any);
    else if (entry_cls_id == SYM_CLASS_TUPLE ||
             entry_cls_id >= SYM_CLASS_EXCEPTION)
        lily_gc_collect_tuple(value_type, value.list);
    else if (entry_cls_id == SYM_CLASS_FUNCTION)
        lily_gc_collect_function(value_type, value.function);
    else
        lily_deref_raw(value_type, value);
}
