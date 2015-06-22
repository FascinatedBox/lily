#include "lily_alloc.h"
#include "lily_core_types.h"
#include "lily_value.h"

lily_function_val *lily_new_foreign_function_val(lily_foreign_func func,
        char *class_name, char *name)
{
    lily_function_val *f = lily_malloc(sizeof(lily_function_val));

    f->refcount = 1;
    f->class_name = class_name;
    f->trace_name = name;
    f->foreign_func = func;
    f->code = NULL;
    f->closure_data = NULL;
    f->gc_entry = NULL;
    f->reg_info = NULL;
    f->reg_count = -1;
    f->has_generics = 0;
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
    f->closure_data = NULL;
    f->gc_entry = NULL;
    f->reg_info = NULL;
    f->reg_count = -1;
    f->has_generics = 0;
    return f;
}

lily_function_val *lily_new_function_copy(lily_function_val *to_copy)
{
    lily_function_val *f = lily_malloc(sizeof(lily_function_val));

    *f = *to_copy;
    return f;
}

void lily_gc_function_marker(int pass, lily_value *v)
{
    lily_function_val *function_val = v->value.function;
    if (function_val->gc_entry &&
        function_val->gc_entry->last_pass != pass) {
        function_val->gc_entry->last_pass = pass;

        lily_closure_data *d = function_val->closure_data;
        int i;
        for (i = 0;i < d->num_upvalues;i++) {
            lily_value *v = d->upvalues[i];
            /* If whatever this thing is can't be deref'd, then it either
               doesn't exist or doesn't have a marker function. */
            if ((v->flags & VAL_IS_NOT_DEREFABLE) == 0) {
                gc_marker_func marker_func = v->type->cls->gc_marker;
                if (marker_func)
                    marker_func(pass, v);
            }
        }
    }
}

void lily_destroy_function(lily_value *v)
{
    lily_function_val *fv = v->value.function;

    if (fv->closure_data) {
        /* Functions only have a gc_entry set if they're a closure. */
        if (fv->gc_entry)
            fv->gc_entry->value.generic = NULL;

        lily_closure_data *d = fv->closure_data;
        d->refcount--;
        if (d->refcount == 0) {
            int i;
            for (i = 0;i < d->num_upvalues;i++) {
                lily_value *up = d->upvalues[i];
                lily_deref(up);
                lily_free(up);
            }

            lily_free(d->upvalues);
            lily_free(d);
        }
    }
    else {
        if (fv->reg_info != NULL) {
            int i;
            for (i = 0;i < fv->reg_count;i++)
                lily_free(fv->reg_info[i].name);
        }

        lily_free(fv->reg_info);
        lily_free(fv->code);
    }
    lily_free(fv);
}

void lily_gc_collect_function(lily_type *function_type,
        lily_function_val *fv)
{
    int marked = 0;
    if (fv->gc_entry == NULL ||
        (fv->gc_entry->last_pass != -1 &&
         fv->gc_entry->value.generic != NULL)) {

        if (fv->gc_entry) {
            fv->gc_entry->last_pass = -1;
            marked = 1;
        }

        int i;
        lily_closure_data *d = fv->closure_data;

        /* The gc only attempts to collect a function if a given function has
           upvalues inside of it. Each function that is copied is given a
           shallow copy of the closure data. As such, this function should never
           attempt to destroy anything but the function_val and the upvalues. */
        d->refcount--;
        if (d->refcount == 0) {
            for (i = 0;i < d->num_upvalues;i++) {
                lily_value *up = d->upvalues[i];
                if ((up->flags & VAL_IS_NOT_DEREFABLE) == 0) {
                    lily_raw_value v = up->value;
                    if (v.generic->refcount == 1)
                        lily_gc_collect_value(up->type, v);
                    else
                        v.generic->refcount--;
                }
                lily_free(up);
            }
            lily_free(d->upvalues);
            lily_free(d);
        }

        if (marked == 0)
            lily_free(fv);
    }
}
