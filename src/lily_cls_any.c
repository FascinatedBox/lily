#include "lily_alloc.h"
#include "lily_vm.h"
#include "lily_value.h"

lily_any_val *lily_new_any_val()
{
    lily_any_val *a = lily_malloc(sizeof(lily_any_val));

    a->inner_value = lily_malloc(sizeof(lily_value));
    a->inner_value->flags = VAL_IS_NIL;
    a->inner_value->type = NULL;
    a->inner_value->value.integer = 0;
    a->gc_entry = NULL;
    a->refcount = 1;

    return a;
}

int lily_any_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    int ret;

    if (*depth == 100)
        lily_raise(vm->raiser, lily_RecursionError, "Infinite loop in comparison.\n");

    lily_value *left_inner = left->value.any->inner_value;
    lily_value *right_inner = right->value.any->inner_value;
    if (left_inner->type == right_inner->type) {
        class_eq_func eq_func = left_inner->type->cls->eq_func;

        (*depth)++;
        ret = eq_func(vm, depth, left_inner, right_inner);
        (*depth)--;
    }
    else
        ret = 0;

    return ret;
}

void lily_gc_any_marker(int pass, lily_value *v)
{
    lily_any_val *any_val = v->value.any;

    if (any_val->gc_entry->last_pass != pass) {
        any_val->gc_entry->last_pass = pass;
        lily_value *inner_value = any_val->inner_value;

        if (inner_value->type->cls->gc_marker != NULL)
            (*inner_value->type->cls->gc_marker)(pass, inner_value);
    }
}

void lily_destroy_any(lily_value *v)
{
    lily_any_val *av = v->value.any;

    /* Values of type 'any' always have a gc entry, so make sure the value of it
       is set to NULL. This prevents the gc from trying to access this 'any'
       that is about to be destroyed. */
    av->gc_entry->value.generic = NULL;

    lily_deref(av->inner_value);

    lily_free(av->inner_value);
    lily_free(av);
}

void lily_gc_collect_any(lily_any_val *any_val)
{
    if (any_val->gc_entry->value.generic != NULL &&
        any_val->gc_entry->last_pass != -1) {
        /* Setting ->last_pass to -1 indicates that everything within the any
           has been free'd except the any. The gc will free the any once
           all inner values have been deref'd/deleted. */
        any_val->gc_entry->last_pass = -1;
        lily_value *inner_value = any_val->inner_value;
        if ((inner_value->flags & VAL_IS_NOT_DEREFABLE) == 0) {
            lily_generic_val *generic_val = inner_value->value.generic;
            if (generic_val->refcount == 1)
                lily_gc_collect_value(inner_value->type,
                        inner_value->value);
            else
                generic_val->refcount--;
        }

        lily_free(any_val->inner_value);
        /* Do not free any_val here: Let the gc do that later. */
    }
}
