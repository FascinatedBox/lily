#include "lily_alloc.h"
#include "lily_core_types.h"
#include "lily_value.h"
#include "lily_vm.h"

extern lily_gc_entry *lily_gc_stopper;

lily_function_val *lily_new_foreign_function_val(lily_foreign_func func,
        char *class_name, char *name)
{
    lily_function_val *f = lily_malloc(sizeof(lily_function_val));

    f->refcount = 1;
    f->class_name = class_name;
    f->trace_name = name;
    f->foreign_func = func;
    f->code = NULL;
    f->num_upvalues = 0;
    f->upvalues = NULL;
    f->gc_entry = NULL;
    f->reg_count = -1;
    return f;
}

lily_function_val *lily_new_native_function_val(char *class_name,
        char *name)
{
    lily_function_val *f = lily_malloc(sizeof(lily_function_val));

    f->refcount = 1;
    f->class_name = class_name;
    f->trace_name = name;
    f->foreign_func = NULL;
    f->code = NULL;
    f->num_upvalues = 0;
    f->upvalues = NULL;
    f->gc_entry = NULL;
    f->reg_count = -1;
    return f;
}

lily_function_val *lily_new_function_copy(lily_function_val *to_copy)
{
    lily_function_val *f = malloc(sizeof(lily_function_val));

    *f = *to_copy;
    return f;
}

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
