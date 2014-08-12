/*  This file contains implementations of functions needed for seeding the
    symtab (see lily_seed_symtab.h for details.
    These functions have been collected here for easy reference for creating
    new per-class functions.
    These are also here so that only lily_seed_symtab will see them. */
#include <string.h>

#include "lily_syminfo.h"
#include "lily_vm.h"

/** integer **/

int lily_integer_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    int ret;

    if (((left->flags & VAL_IS_NIL) == 0) &&
        ((right->flags & VAL_IS_NIL) == 0) &&
        left->value.integer == right->value.integer)
        ret = 1;
    else if ((left->flags & VAL_IS_NIL) && (right->flags & VAL_IS_NIL))
        ret = 1;
    else
        ret = 0;

    return ret;
}

/** double **/

int lily_double_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    int ret;

    if (((left->flags & VAL_IS_NIL) == 0) &&
        ((right->flags & VAL_IS_NIL) == 0) &&
        left->value.doubleval == right->value.doubleval)
        ret = 1;
    else if ((left->flags & VAL_IS_NIL) && (right->flags & VAL_IS_NIL))
        ret = 1;
    else
        ret = 0;

    return ret;
}

/** str **/

int lily_string_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    int ret;

    if (((left->flags & VAL_IS_NIL) == 0) &&
        ((right->flags & VAL_IS_NIL) == 0) &&
        left->value.string->size == right->value.string->size &&
        (left->value.string == right->value.string ||
         strcmp(left->value.string->string, right->value.string->string) == 0))
        ret = 1;
    else if ((left->flags & VAL_IS_NIL) && (right->flags & VAL_IS_NIL))
        ret = 1;
    else
        ret = 0;

    return ret;
}

/* any */

void lily_gc_any_marker(int pass, lily_value *v)
{
    lily_any_val *any_val = v->value.any;

    if (any_val->gc_entry->last_pass != pass) {
        any_val->gc_entry->last_pass = pass;
        lily_value *inner_value = any_val->inner_value;

        if ((inner_value->flags & VAL_IS_NIL) == 0 &&
            inner_value->sig->cls->gc_marker != NULL) {
            (*inner_value->sig->cls->gc_marker)(pass, inner_value);
        }
    }
}

int lily_any_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    int ret;

    if (*depth == 100)
        lily_raise(vm->raiser, lily_ErrRecursion, "Infinite loop in comparison.\n");

    if (((left->flags & VAL_IS_NIL) == 0) &&
        ((right->flags & VAL_IS_NIL) == 0)) {
        lily_value *left_inner = left->value.any->inner_value;
        lily_value *right_inner = right->value.any->inner_value;
        if (left_inner->sig == right_inner->sig) {
            class_eq_func eq_func = left_inner->sig->cls->eq_func;

            (*depth)++;
            ret = eq_func(vm, depth, left_inner, right_inner);
            (*depth)--;
        }
        else
            ret = 0;
    }
    else if ((left->flags & VAL_IS_NIL) && (right->flags & VAL_IS_NIL))
        ret = 1;
    else
        ret = 0;

    return ret;
}

/* list */

void lily_gc_list_marker(int pass, lily_value *v)
{
    lily_list_val *list_val = v->value.list;
    int i;

    if (list_val->gc_entry &&
        list_val->gc_entry->last_pass != pass) {
        list_val->gc_entry->last_pass = pass;

        lily_sig *elem_sig = v->sig->siglist[0];
        void (*gc_marker)(int, lily_value *);

        gc_marker = elem_sig->cls->gc_marker;

        if (gc_marker) {
            for (i = 0;i < list_val->num_values;i++) {
                lily_value *elem = list_val->elems[i];

                if ((elem->flags & VAL_IS_NIL) == 0)
                    gc_marker(pass, elem);
            }
        }
    }
}

int lily_list_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    if (*depth == 100)
        lily_raise(vm->raiser, lily_ErrRecursion, "Infinite loop in comparison.\n");

    int ret;

    if (((left->flags & VAL_IS_NIL) == 0) &&
        ((right->flags & VAL_IS_NIL) == 0) &&
        left->value.list->num_values == right->value.list->num_values) {
        class_eq_func eq_func = left->sig->siglist[0]->cls->eq_func;
        lily_value **left_elems = left->value.list->elems;
        lily_value **right_elems = right->value.list->elems;

        int i;
        ret = 1;

        for (i = 0;i < left->value.list->num_values;i++) {
            (*depth)++;
            if (eq_func(vm, depth, left_elems[i], right_elems[i]) == 0) {
                ret = 0;
                (*depth)--;
                break;
            }
            (*depth)--;
        }
    }
    else if ((left->flags & VAL_IS_NIL) && (right->flags & VAL_IS_NIL))
        ret = 1;
    else
        ret = 0;

    return ret;
}

/* hash */

void lily_gc_hash_marker(int pass, lily_value *v)
{
    lily_hash_val *hash_val = v->value.hash;
    if (hash_val->gc_entry &&
        hash_val->gc_entry->last_pass != pass) {
        hash_val->gc_entry->last_pass = pass;

        lily_sig *hash_value_sig = v->sig->siglist[1];
        void (*gc_marker)(int, lily_value *);

        gc_marker = hash_value_sig->cls->gc_marker;

        lily_hash_elem *elem_iter = hash_val->elem_chain;
        while (elem_iter) {
            lily_value *elem_value = elem_iter->elem_value;
            if ((elem_value->flags & VAL_IS_NIL) == 0)
                gc_marker(pass, elem_value);

            elem_iter = elem_iter->next;
        }
    }
}

int lily_hash_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    if (*depth == 100)
        lily_raise(vm->raiser, lily_ErrRecursion, "Infinite loop in comparison.\n");

    int ret;

    if (((left->flags & VAL_IS_NIL) == 0) &&
        ((right->flags & VAL_IS_NIL) == 0) &&
        left->value.hash->num_elems == right->value.hash->num_elems) {
        lily_hash_val *left_hash = left->value.hash;
        lily_hash_val *right_hash = right->value.hash;

        class_eq_func key_eq_func = left->sig->siglist[0]->cls->eq_func;
        class_eq_func value_eq_func = left->sig->siglist[1]->cls->eq_func;

        lily_hash_elem *left_iter = left_hash->elem_chain;
        lily_hash_elem *right_iter;
        lily_hash_elem *right_start = right_hash->elem_chain;
        /* Assume success, in case the hash is empty. */
        ret = 1;
        for (left_iter = left_hash->elem_chain;
             left_iter != NULL;
             left_iter = left_iter->next) {
            (*depth)++;
            /* If there's a match, this will get set to 1 again. */
            ret = 0;
            for (right_iter = right_start;
                 right_iter != NULL;
                 right_iter = right_iter->next) {
                /* First check that the siphashes are near before doing
                   anything fancy. */
                if (left_iter->key_siphash == right_iter->key_siphash) {
                    /* Keys are proper Lily values, so check that the keys
                       totally match before checking the value. */
                    if (key_eq_func(vm, depth, left_iter->elem_key,
                        right_iter->elem_key)) {
                        /* If the key is a match, then the result depends on if
                           the values match. Otherwise, skip to the next key. */
                        ret = (value_eq_func(vm, depth, left_iter->elem_value,
                                right_iter->elem_value));

                        /* If the first entry was a match, begin all subsequent
                           searches at the one after it. This whittles down the
                           search size over time if both hashes have the keys
                           in the same order. Unlikely, maybe, but a simple
                           check compared to a good gain. */
                        if (right_iter == right_start)
                            right_start = right_start->next;
                        break;
                    }
                }
            }
            (*depth)--;

            if (ret == 0)
                break;
        }
    }
    else if ((left->flags & VAL_IS_NIL) && (right->flags & VAL_IS_NIL))
        ret = 1;
    else
        ret = 0;

    return ret;
}

/* tuple */

void lily_gc_tuple_marker(int pass, lily_value *v)
{
    lily_list_val *tuple_val = v->value.list;
    int i;

    if (tuple_val->gc_entry &&
        tuple_val->gc_entry->last_pass != pass) {
        tuple_val->gc_entry->last_pass = pass;

        for (i = 0;i < tuple_val->num_values;i++) {
            lily_value *elem = tuple_val->elems[i];

            void (*gc_marker)(int, lily_value *) = elem->sig->cls->gc_marker;

            if (gc_marker && (elem->flags & VAL_IS_NIL) == 0)
                gc_marker(pass, elem);
        }
    }
}

int lily_tuple_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    if (*depth == 100)
        lily_raise(vm->raiser, lily_ErrRecursion, "Infinite loop in comparison.\n");

    int ret;

    if (((left->flags & VAL_IS_NIL) == 0) &&
        ((right->flags & VAL_IS_NIL) == 0) &&
        left->value.list->num_values == right->value.list->num_values) {
        lily_value **left_elems = left->value.list->elems;
        lily_value **right_elems = right->value.list->elems;

        int i;
        ret = 1;

        for (i = 0;i < left->value.list->num_values;i++) {
            class_eq_func eq_func = left->sig->siglist[i]->cls->eq_func;
            (*depth)++;
            if (eq_func(vm, depth, left_elems[i], right_elems[i]) == 0) {
                ret = 0;
                (*depth)--;
                break;
            }
            (*depth)--;
        }
    }
    else if ((left->flags & VAL_IS_NIL) && (right->flags & VAL_IS_NIL))
        ret = 1;
    else
        ret = 0;

    return ret;
}

/* generic */

/* This is used by function for == and !=. It's a simple pointer comparison. */
int lily_generic_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    int ret;

    if ((left->flags & VAL_IS_NIL) == 0 &&
        (right->flags & VAL_IS_NIL) == 0 &&
        left->value.generic == right->value.generic) {
        ret = 1;
    }
    else if ((left->flags & VAL_IS_NIL) && (right->flags & VAL_IS_NIL))
        ret = 1;
    else
        ret = 0;

    return ret;
}
