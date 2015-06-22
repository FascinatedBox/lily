#include "lily_alloc.h"
#include "lily_core_types.h"
#include "lily_vm.h"
#include "lily_value.h"

int lily_tuple_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    if (*depth == 100)
        lily_raise(vm->raiser, lily_RecursionError, "Infinite loop in comparison.\n");

    int ret;

    if (left->value.list->num_values == right->value.list->num_values) {
        lily_value **left_elems = left->value.list->elems;
        lily_value **right_elems = right->value.list->elems;

        int i;
        ret = 1;

        for (i = 0;i < left->value.list->num_values;i++) {
            class_eq_func eq_func = left->type->subtypes[i]->cls->eq_func;
            (*depth)++;
            if (eq_func(vm, depth, left_elems[i], right_elems[i]) == 0) {
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

void lily_gc_tuple_marker(int pass, lily_value *v)
{
    lily_list_val *tuple_val = v->value.list;
    int i;

    if (tuple_val->gc_entry &&
        tuple_val->gc_entry->last_pass != pass) {
        tuple_val->gc_entry->last_pass = pass;

        for (i = 0;i < tuple_val->num_values;i++) {
            lily_value *elem = tuple_val->elems[i];

            void (*gc_marker)(int, lily_value *) = elem->type->cls->gc_marker;

            if (gc_marker && (elem->flags & VAL_IS_NIL) == 0)
                gc_marker(pass, elem);
        }
    }
}

void lily_destroy_tuple(lily_value *v)
{
    lily_list_val *tv = v->value.list;

    /* If this tuple has a gc entry, then make the value of it NULL. This
       prevents the gc from trying to access the tuple once it has been
       destroyed. */
    if (tv->gc_entry != NULL)
        tv->gc_entry->value.generic = NULL;

    int i;
    for (i = 0;i < tv->num_values;i++) {
        lily_value *elem_val = tv->elems[i];

        lily_deref(elem_val);

        lily_free(elem_val);
    }

    lily_free(tv->elems);
    lily_free(tv);
}

void lily_gc_collect_tuple(lily_type *tuple_type,
        lily_list_val *tuple_val)
{
    int marked = 0;
    if (tuple_val->gc_entry == NULL ||
        (tuple_val->gc_entry->last_pass != -1 &&
         tuple_val->gc_entry->value.generic != NULL)) {

        if (tuple_val->gc_entry) {
            tuple_val->gc_entry->last_pass = -1;
            marked = 1;
        }

        int i;

        for (i = 0;i < tuple_val->num_values;i++) {
            lily_value *elem = tuple_val->elems[i];
            /* Each value the tuple holds may or may not be refcounted, so they
               must be checked individually. */
            if ((elem->flags & VAL_IS_NOT_DEREFABLE) == 0) {
                lily_raw_value v = elem->value;
                if (v.generic->refcount == 1)
                    lily_gc_collect_value(elem->type, v);
                else
                    v.generic->refcount--;
            }
            lily_free(elem);
        }

        lily_free(tuple_val->elems);
        if (marked == 0)
            lily_free(tuple_val);
    }
}
