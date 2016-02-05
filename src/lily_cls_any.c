#include "lily_alloc.h"
#include "lily_vm.h"
#include "lily_value.h"

lily_any_val *lily_new_any_val()
{
    lily_any_val *a = lily_malloc(sizeof(lily_any_val));

    a->inner_value = lily_new_value(0,
            (lily_raw_value){.integer = 0});
    a->gc_entry = NULL;
    a->refcount = 1;

    return a;
}

void lily_gc_any_marker(int pass, lily_value *v)
{
    lily_value *inner_value = v->value.any->inner_value;

    if (inner_value->flags & VAL_IS_GC_TAGGED)
        lily_gc_mark(pass, inner_value);
}

void lily_destroy_any(lily_value *v)
{
    lily_any_val *av = v->value.any;

    /* Type 'any' always has a marker, but enums are laid out just like 'any'.
       Enums, unlike 'any', don't always get a gc marker. */
    if (av->gc_entry)
        av->gc_entry->value.generic = NULL;

    lily_deref(av->inner_value);

    lily_free(av->inner_value);
    lily_free(av);
}

void lily_gc_collect_any(lily_value *v)
{
    lily_any_val *any_val = v->value.any;
    if (any_val->gc_entry->value.generic != NULL &&
        any_val->gc_entry->last_pass != -1) {
        /* Setting ->last_pass to -1 indicates that everything within the any
           has been free'd except the any. The gc will free the any once
           all inner values have been deref'd/deleted. */
        any_val->gc_entry->last_pass = -1;
        lily_value *inner_value = any_val->inner_value;
        if (inner_value->flags & VAL_IS_DEREFABLE) {
            lily_generic_val *generic_val = inner_value->value.generic;
            if (generic_val->refcount == 1)
                lily_gc_collect_value(inner_value);
            else
                generic_val->refcount--;
        }

        lily_free(any_val->inner_value);
        /* Do not free any_val here: Let the gc do that later. */
    }
}
