#include "lily_alloc.h"
#include "lily_core_types.h"
#include "lily_value.h"
#include "lily_vm.h"

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

    if (fv->upvalues) {
        /* Functions only have a gc_entry set if they're a closure. */
        if (fv->gc_entry)
            fv->gc_entry->value.generic = NULL;

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
    }
    else
        lily_free(fv->code);

    lily_free(fv);
}

void lily_gc_collect_function(lily_value *v)
{
    lily_function_val *fv = v->value.function;
    int marked = 0;
    if (fv->gc_entry == NULL ||
        (fv->gc_entry->last_pass != -1 &&
         fv->gc_entry->value.generic != NULL)) {

        if (fv->gc_entry) {
            fv->gc_entry->last_pass = -1;
            marked = 1;
        }

        lily_value **upvalues = fv->upvalues;
        int count = fv->num_upvalues;
        int i;

        for (i = 0;i < count;i++) {
            lily_value *up = upvalues[i];
            if (up) {
                up->cell_refcount--;

                if (up->cell_refcount == 0) {
                    if (up->flags & VAL_IS_DEREFABLE) {
                        lily_raw_value v = up->value;
                        if (v.generic->refcount == 1)
                            lily_collect_value(up);
                        else
                            v.generic->refcount--;
                    }
                    lily_free(up);
                }
            }
        }
        lily_free(upvalues);

        if (marked == 0)
            lily_free(fv);
    }
}
