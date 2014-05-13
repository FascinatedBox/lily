#include "lily_impl.h"
#include "lily_gc.h"
#include "lily_value.h"

/** gc collection functions
    These are responsible for tearing everything inside a value. These don't
    delete the actual values though, to protect against invalid reads when
    handling the nastier circular references.
    These should all set the gc_entry's last_pass to -1 so that the gc will
    delete the actual value when it is safe to do so. **/

void lily_gc_collect_value(lily_sig *value_sig, lily_raw_value value)
{
    int entry_cls_id = value_sig->cls->id;

    if (entry_cls_id == SYM_CLASS_LIST)
        lily_gc_collect_list(value_sig, value.list);
    else if (entry_cls_id == SYM_CLASS_HASH)
        lily_gc_collect_hash(value_sig, value.hash);
    else if (entry_cls_id == SYM_CLASS_OBJECT)
        lily_gc_collect_object(value.object);
    else
        lily_deref_unknown_raw_val(value_sig, value);
}

void lily_gc_collect_object(lily_object_val *object_val)
{
    if (object_val->gc_entry->value.generic != NULL &&
        object_val->gc_entry->last_pass != -1) {
        /* Setting ->last_pass to -1 indicates that everything within the object
           has been free'd except the object. The gc will free the object once
           all inner values have been deref'd/deleted. */
        object_val->gc_entry->last_pass = -1;
        lily_value *inner_value = object_val->inner_value;
        if ((inner_value->flags & SYM_IS_NIL) == 0 &&
            inner_value->sig->cls->is_refcounted) {
            lily_generic_val *generic_val = inner_value->value.generic;
            if (generic_val->refcount == 1)
                lily_gc_collect_value(inner_value->sig, inner_value->value);
            else
                generic_val->refcount--;
        }

        lily_free(object_val->inner_value);
        /* Do not free object_val here: Let the gc do that later. */
    }
}

void lily_gc_collect_list(lily_sig *list_sig, lily_list_val *list_val)
{
    /* The first check is done because this list might be inside of an object
       that is being collected. So it may not be in the gc, but it needs to be
       destroyed because it was trapped in a circular ref.
       The second check acts as a 'lock' to make sure that this cannot be done
       twice for the same list, thus preventing recursion. */
    int marked = 0;
    if (list_val->gc_entry == NULL ||
        (list_val->gc_entry->last_pass != -1 &&
         list_val->gc_entry->value.generic != NULL)) {

        lily_sig *value_sig = list_sig->siglist[0];

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
        if (value_sig->cls->is_refcounted) {
            for (i = 0;i < list_val->num_values;i++) {
                /* Pass stuff off to the gc to collect. This will use a typical
                   deref for stuff like str. */
                lily_value *elem = list_val->elems[i];
                if ((elem->flags & SYM_IS_NIL) == 0) {
                    lily_raw_value v = elem->value;
                    if (v.generic->refcount == 1)
                        lily_gc_collect_value(value_sig, v);
                    else
                        v.generic->refcount--;
                }
                lily_free(elem);
            }
        }
        else {
            /* Still need to free all the list elements, even if not
               refcounted. */
            for (i = 0;i < list_val->num_values;i++)
                lily_free(list_val->elems[i]);
        }
        /* else the values aren't refcounted (ex: list[integer]). No-op. */

        lily_free(list_val->elems);
        if (marked == 0)
            lily_free(list_val);
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

    * hash_sig: The signature of the hash given.
    * hash_val: The hash value to destroy the inner values of. If this hash
                value does not have a gc entry, it will be deleted as well. */
void lily_gc_collect_hash(lily_sig *hash_sig, lily_hash_val *hash_val)
{
    int marked = 0;
    if (hash_val->gc_entry == NULL ||
        (hash_val->gc_entry->last_pass != -1 &&
         hash_val->gc_entry->value.generic != NULL)) {

        lily_sig *hash_value_sig = hash_sig->siglist[1];

        if (hash_val->gc_entry) {
            hash_val->gc_entry->last_pass = -1;

            marked = 1;
        }

        if (hash_value_sig->cls->is_refcounted) {
            lily_hash_elem *elem_iter = hash_val->elem_chain;
            lily_hash_elem *elem_temp;
            while (elem_iter) {
                lily_value *elem_value = elem_iter->elem_value;
                elem_temp = elem_iter->next;
                if ((elem_value->flags & SYM_IS_NIL) == 0) {
                    lily_raw_value v = elem_value->value;
                    if (v.generic->refcount == 1)
                        lily_gc_collect_value(hash_value_sig, v);
                    else
                        v.generic->refcount--;
                }
                lily_free(elem_iter->elem_key);
                lily_free(elem_iter->elem_value);
                lily_free(elem_iter);
                elem_iter = elem_temp;
            }
        }

        if (marked == 0)
            lily_free(hash_val);
    }
}

/** gc 'marker' functions.
    These are responsible for diving into values with a gc_entry attached and
    setting ->last_pass to the pass given. This is how the mark phase of the
    mark-and-sweep GC works. **/

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
            if ((elem_value->flags & SYM_IS_NIL) == 0)
                gc_marker(pass, elem_value);

            elem_iter = elem_iter->next;
        }
    }
}

void lily_gc_object_marker(int pass, lily_value *v)
{
    lily_object_val *obj_val = v->value.object;

    if (obj_val->gc_entry->last_pass != pass) {
        obj_val->gc_entry->last_pass = pass;
        lily_value *inner_value = obj_val->inner_value;

        if ((inner_value->flags & SYM_IS_NIL) == 0 &&
            inner_value->sig->cls->gc_marker != NULL) {
            (*inner_value->sig->cls->gc_marker)(pass, inner_value);
        }
    }
}

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

                if ((elem->flags & SYM_IS_NIL) == 0)
                    gc_marker(pass, elem);
            }
        }
    }
}
