#include "lily_alloc.h"
#include "lily_core_types.h"
#include "lily_vm.h"

#include "lily_api_value.h"

extern lily_gc_entry *lily_gc_stopper;

void lily_gc_function_marker(int pass, lily_value *v)
{
    lily_function_val *function_val = v->value.function;

    lily_value **upvalues = function_val->upvalues;
    int count = function_val->num_upvalues;
    int i;

    for (i = 0;i < count;i++) {
        lily_value *up = upvalues[i];
        /* If whatever this thing is can't be deref'd, then it either
            doesn't exist or doesn't have a marker function. */
        if (up && (up->flags & VAL_IS_GC_SWEEPABLE))
            lily_gc_mark(pass, up);
    }
}

void lily_destroy_function(lily_value *v)
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
