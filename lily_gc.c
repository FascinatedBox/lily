#include "lily_impl.h"
#include "lily_gc.h"
#include "lily_symtab.h"

/** gc collection functions
    These are responsible for tearing everything inside a value. These don't
    delete the actual values though, to protect against invalid reads when
    handling the nastier circular references.
    These should all set the gc_entry's last_pass to -1 so that the gc will
    delete the actual value when it is safe to do so. **/

void lily_gc_collect_value(lily_sig *value_sig, lily_value value)
{
    int entry_cls_id = value_sig->cls->id;

    if (entry_cls_id == SYM_CLASS_LIST)
        lily_gc_collect_list(value_sig, value.list);
    else if (entry_cls_id == SYM_CLASS_OBJECT)
        lily_gc_collect_object(value.object);
    else
        lily_deref_unknown_val(value_sig, value);
}

void lily_gc_collect_object(lily_object_val *object_val)
{
    if (object_val->gc_entry->value.generic != NULL &&
        object_val->gc_entry->last_pass != -1) {
        /* Setting ->last_pass to -1 indicates that everything within the object
           has been free'd except the object. The gc will free the object once
           all inner values have been deref'd/deleted. */
        object_val->gc_entry->last_pass = -1;
        if (object_val->sig && object_val->sig->cls->is_refcounted) {
            lily_generic_val *generic_val = object_val->value.generic;
            if (generic_val->refcount == 1)
                lily_gc_collect_value(object_val->sig, object_val->value);
            else
                generic_val->refcount--;
        }

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

        /* This is important because this could be a list[str], and the strings
           will need to be free'd. */
        if (value_sig->cls->is_refcounted) {
            int i;
            for (i = 0;i < list_val->num_values;i++) {
                /* Pass stuff off to the gc to collect. This will use a typical
                   deref for stuff like str. */
                if ((list_val->flags[i] & SYM_IS_NIL) == 0) {
                    lily_value v = list_val->values[i];
                    if (v.generic->refcount == 1)
                        lily_gc_collect_value(value_sig, v);
                    else
                        v.generic->refcount--;
                }
            }
        }
        /* else the values aren't refcounted (ex: list[integer]). No-op. */

        lily_free(list_val->flags);
        lily_free(list_val->values);
        if (marked == 0)
            lily_free(list_val);
    }
}

/** gc 'marker' functions.
    These are responsible for diving into values with a gc_entry attached and
    setting ->last_pass to the pass given. This is how the mark phase of the
    mark-and-sweep GC works. **/

void lily_gc_hash_marker(int pass, lily_sig *value_sig, lily_value v)
{
    /* todo: Do this after doing static hashes. */
    return;
}

void lily_gc_object_marker(int pass, lily_sig *value_sig, lily_value v)
{
    lily_object_val *obj_val = v.object;

    if (obj_val->gc_entry->last_pass != pass) {
        obj_val->gc_entry->last_pass = pass;
        if (obj_val->sig != NULL &&
            obj_val->sig->cls->gc_marker != NULL) {
            (*obj_val->sig->cls->gc_marker)(pass, obj_val->sig, obj_val->value);
        }
    }
}

void lily_gc_list_marker(int pass, lily_sig *value_sig, lily_value v)
{
    lily_list_val *list_val = v.list;
    int i;

    if (list_val->gc_entry &&
        list_val->gc_entry->last_pass != pass) {
        list_val->gc_entry->last_pass = pass;

        lily_sig *elem_sig = value_sig->siglist[0];
        void (*gc_marker)(int, lily_sig *, lily_value);

        gc_marker = elem_sig->cls->gc_marker;

        if (gc_marker) {
            for (i = 0;i < list_val->num_values;i++) {
                if ((list_val->flags[i] & SYM_IS_NIL) == 0)
                    gc_marker(pass, elem_sig, list_val->values[i]);
            }
        }
    }
}
