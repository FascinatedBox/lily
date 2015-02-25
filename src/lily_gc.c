#include "lily_impl.h"
#include "lily_gc.h"
#include "lily_value.h"

/** gc collection functions
    These are responsible for tearing everything inside a value. These don't
    delete the actual values though, to protect against invalid reads when
    handling the nastier circular references.
    These should all set the gc_entry's last_pass to -1 so that the gc will
    delete the actual value when it is safe to do so. **/

#define free_mem(ptr) (void)mem_func(ptr, 0)

void lily_gc_collect_value(lily_mem_func mem_func, lily_type *value_type,
        lily_raw_value value)
{
    int entry_cls_id = value_type->cls->id;

    if (entry_cls_id == SYM_CLASS_LIST)
        lily_gc_collect_list(mem_func, value_type, value.list);
    else if (entry_cls_id == SYM_CLASS_HASH)
        lily_gc_collect_hash(mem_func, value_type, value.hash);
    else if (value_type->cls->flags & CLS_ENUM_CLASS)
        lily_gc_collect_any(mem_func, value.any);
    else if (entry_cls_id == SYM_CLASS_TUPLE ||
             entry_cls_id >= SYM_CLASS_EXCEPTION)
        lily_gc_collect_tuple(mem_func, value_type, value.list);
    else
        lily_deref_unknown_raw_val(mem_func, value_type, value);
}

void lily_gc_collect_any(lily_mem_func mem_func, lily_any_val *any_val)
{
    if (any_val->gc_entry->value.generic != NULL &&
        any_val->gc_entry->last_pass != -1) {
        /* Setting ->last_pass to -1 indicates that everything within the any
           has been free'd except the any. The gc will free the any once
           all inner values have been deref'd/deleted. */
        any_val->gc_entry->last_pass = -1;
        lily_value *inner_value = any_val->inner_value;
        if ((inner_value->flags & VAL_IS_NIL_OR_PROTECTED) == 0 &&
            inner_value->type->cls->is_refcounted) {
            lily_generic_val *generic_val = inner_value->value.generic;
            if (generic_val->refcount == 1)
                lily_gc_collect_value(mem_func, inner_value->type,
                        inner_value->value);
            else
                generic_val->refcount--;
        }

        free_mem(any_val->inner_value);
        /* Do not free any_val here: Let the gc do that later. */
    }
}

void lily_gc_collect_list(lily_mem_func mem_func, lily_type *list_type,
        lily_list_val *list_val)
{
    /* The first check is done because this list might be inside of an any
       that is being collected. So it may not be in the gc, but it needs to be
       destroyed because it was trapped in a circular ref.
       The second check acts as a 'lock' to make sure that this cannot be done
       twice for the same list, thus preventing recursion. */
    int marked = 0;
    if (list_val->gc_entry == NULL ||
        (list_val->gc_entry->last_pass != -1 &&
         list_val->gc_entry->value.generic != NULL)) {

        lily_type *value_type = list_type->subtypes[0];

        if (list_val->gc_entry) {
            list_val->gc_entry->last_pass = -1;
            /* If this list has a gc entry, then it can contains elements which
               refer to itself. Set last_pass to -1 to indicate that everything
               inside this list has already been deleted. The gc will delete the
               list later. */
            marked = 1;
        }

        int i;

        /* This is important because this could be a list[str], and the strings
           will need to be free'd. */
        if (value_type->cls->is_refcounted) {
            for (i = 0;i < list_val->num_values;i++) {
                /* Pass stuff off to the gc to collect. This will use a typical
                   deref for stuff like string. */
                lily_value *elem = list_val->elems[i];
                if ((elem->flags & VAL_IS_NIL_OR_PROTECTED) == 0) {
                    lily_raw_value v = elem->value;
                    if (v.generic->refcount == 1)
                        lily_gc_collect_value(mem_func, value_type, v);
                    else
                        v.generic->refcount--;
                }
                free_mem(elem);
            }
        }
        else {
            /* Still need to free all the list elements, even if not
               refcounted. */
            for (i = 0;i < list_val->num_values;i++)
                free_mem(list_val->elems[i]);
        }
        /* else the values aren't refcounted (ex: list[integer]). No-op. */

        free_mem(list_val->elems);
        if (marked == 0)
            free_mem(list_val);
    }
}

/*  lily_gc_collect_hash

    This call is responsible for destroying a hash value. This can be called
    directly from the gc through lily_gc_collect_value, or it may be called for
    values inside something circular.

    This code is pretty similar to lily_gc_collect_list, except that the
    elements are in a linked list and only the hash value may be circular.

    The values in the hash (inner values) are deref'd (or deleted if at 1 ref).
    The actual hash is not deleted if there is a gc entry, because subsequent
    entries may circularly link to it. In such a case, the gc will do a final
    sweep of the hash value.

    * hash_type: The type of the hash given.
    * hash_val: The hash value to destroy the inner values of. If this hash
                value does not have a gc entry, it will be deleted as well. */
void lily_gc_collect_hash(lily_mem_func mem_func, lily_type *hash_type,
        lily_hash_val *hash_val)
{
    int marked = 0;
    if (hash_val->gc_entry == NULL ||
        (hash_val->gc_entry->last_pass != -1 &&
         hash_val->gc_entry->value.generic != NULL)) {

        lily_type *hash_key_type = hash_type->subtypes[0];
        lily_type *hash_value_type = hash_type->subtypes[1];

        if (hash_val->gc_entry) {
            hash_val->gc_entry->last_pass = -1;

            marked = 1;
        }

        lily_hash_elem *elem_iter = hash_val->elem_chain;
        lily_hash_elem *elem_temp;
        while (elem_iter) {
            lily_value *elem_value = elem_iter->elem_value;
            lily_value *elem_key = elem_iter->elem_key;

            elem_temp = elem_iter->next;
            if ((elem_key->flags & VAL_IS_NIL_OR_PROTECTED) == 0) {
                lily_raw_value k = elem_key->value;
                if (k.generic->refcount == 1)
                    lily_gc_collect_value(mem_func, hash_key_type, k);
                else
                    k.generic->refcount--;
            }

            if ((elem_value->flags & VAL_IS_NIL_OR_PROTECTED) == 0) {
                lily_raw_value v = elem_value->value;
                if (v.generic->refcount == 1)
                    lily_gc_collect_value(mem_func, hash_value_type, v);
                else
                    v.generic->refcount--;
            }

            free_mem(elem_iter->elem_key);
            free_mem(elem_iter->elem_value);
            free_mem(elem_iter);
            elem_iter = elem_temp;
        }

        if (marked == 0)
            free_mem(hash_val);
    }
}

void lily_gc_collect_tuple(lily_mem_func mem_func, lily_type *tuple_type,
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
            if (elem->type->cls->is_refcounted &&
                (elem->flags & VAL_IS_NIL_OR_PROTECTED) == 0) {
                lily_raw_value v = elem->value;
                if (v.generic->refcount == 1)
                    lily_gc_collect_value(mem_func, elem->type, v);
                else
                    v.generic->refcount--;
            }
            free_mem(elem);
        }

        free_mem(tuple_val->elems);
        if (marked == 0)
            free_mem(tuple_val);
    }
}
