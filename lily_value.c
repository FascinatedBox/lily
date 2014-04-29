#include <stdlib.h>

#include "lily_impl.h"
#include "lily_value.h"

/** Deref-ing calls **/

void lily_deref_hash_val(lily_sig *sig, lily_hash_val *hv)
{
    hv->refcount--;
    if (hv->refcount == 0) {
        lily_sig *value_sig = sig->siglist[1];
        int value_cls_id = value_sig->cls->id;
        int value_is_refcounted = value_sig->cls->is_refcounted;
        lily_hash_elem *elem, *save_next;
        elem = hv->elem_chain;
        while (elem) {
            if (elem->flags == 0 && value_is_refcounted)
                lily_deref_unknown_val(value_sig, elem->value);
            else if (value_cls_id == SYM_CLASS_OBJECT) {
                /* Objects are containers that are not shared. This circularity
                   applies to what's inside the object, not the object itself.
                   Make sure to destroy the object. */
                lily_object_val *ov = elem->value.object;
                lily_free(ov);
            }

            save_next = elem->next;
            lily_free(elem);
            elem = save_next;
        }

        lily_free(hv);
    }
}

void lily_deref_list_val(lily_sig *sig, lily_list_val *lv)
{
    lv->refcount--;
    if (lv->refcount == 0) {
        /* If this list has a gc entry, then make the value of it NULL. This
           prevents the gc from trying to access the list once it has been
           destroyed. */
        if (lv->gc_entry != NULL)
            lv->gc_entry->value.generic = NULL;

        int cls_id = sig->siglist[0]->cls->id;
        int i;
        if (cls_id == SYM_CLASS_LIST) {
            for (i = 0;i < lv->num_values;i++) {
                if (lv->flags[i] == 0)
                    lily_deref_list_val(sig->siglist[0],
                            lv->values[i].list);
            }
        }
        else if (cls_id == SYM_CLASS_HASH) {
            for (i = 0;i < lv->num_values;i++) {
                if (lv->flags[i] == 0)
                    lily_deref_hash_val(sig->siglist[0],
                            lv->values[i].hash);
            }
        }
        else if (cls_id == SYM_CLASS_STR) {
            for (i = 0;i < lv->num_values;i++)
                if (lv->flags[i] == 0)
                    lily_deref_str_val(lv->values[i].str);
        }
        else if (cls_id == SYM_CLASS_METHOD) {
            for (i = 0;i < lv->num_values;i++)
                if (lv->flags[i] == 0)
                    lily_deref_method_val(lv->values[i].method);
        }
        else if (cls_id == SYM_CLASS_OBJECT) {
            for (i = 0;i < lv->num_values;i++) {
                if (lv->flags[i] == 0)
                    lily_deref_object_val(lv->values[i].object);
            }
        }

        lily_free(lv->flags);
        lily_free(lv->values);
        lily_free(lv);
    }
}

void lily_deref_method_val(lily_method_val *mv)
{
    mv->refcount--;
    if (mv->refcount == 0) {
        if (mv->reg_info != NULL) {
            int i;
            for (i = 0;i < mv->reg_count;i++)
                lily_free(mv->reg_info[i].name);
        }

        lily_free(mv->reg_info);
        lily_free(mv->code);
        lily_free(mv);
    }
}

void lily_deref_str_val(lily_str_val *sv)
{
    sv->refcount--;
    if (sv->refcount == 0) {
        if (sv->str)
            lily_free(sv->str);
        lily_free(sv);
    }
}

void lily_deref_object_val(lily_object_val *ov)
{
    ov->refcount--;
    if (ov->refcount == 0) {
        /* Objects always have a gc entry, so make sure the value of it is set
           to NULL. This prevents the gc from trying to access this object that
           is about to be destroyed. */
        ov->gc_entry->value.generic = NULL;

        if (ov->sig != NULL)
            lily_deref_unknown_val(ov->sig, ov->value);

        lily_free(ov);
    }
}

/* lily_deref_unknown_val
   This is a handy function for doing a deref but not knowing what class the
   sig contained. This should be used to keep redundant checking code. In some
   cases, such as list derefs, hoisting the loops is a better idea for speed. */
void lily_deref_unknown_val(lily_sig *sig, lily_value v)
{
    int cls_id = sig->cls->id;
    if (cls_id == SYM_CLASS_LIST)
        lily_deref_list_val(sig, v.list);
    else if (cls_id == SYM_CLASS_STR)
        lily_deref_str_val(v.str);
    else if (cls_id == SYM_CLASS_METHOD)
        lily_deref_method_val(v.method);
    else if (cls_id == SYM_CLASS_OBJECT)
        lily_deref_object_val(v.object);
    else if (cls_id == SYM_CLASS_HASH)
        lily_deref_hash_val(sig, v.hash);
}

/** Value creation calls **/

/* lily_try_new_function_val
   This will attempt to create a new function value (for storing a function
   pointer and a name for it).
   Note: 'try' means this call returns NULL on failure. */
lily_function_val *lily_try_new_function_val(lily_func func, char *name)
{
    lily_function_val *f = lily_malloc(sizeof(lily_function_val));

    if (f == NULL) {
        lily_free(f);
        return NULL;
    }

    f->refcount = 1;
    f->func = func;
    f->trace_name = name;
    return f;
}

/* lily_try_new_method_val
   This will attempt to create a new method value (for storing code and the
   position of it).
   Note: 'try' means this call returns NULL on failure. */
lily_method_val *lily_try_new_method_val()
{
    lily_method_val *m = lily_malloc(sizeof(lily_method_val));
    uintptr_t *code = lily_malloc(8 * sizeof(uintptr_t));

    if (m == NULL || code == NULL) {
        lily_free(m);
        lily_free(code);
        return NULL;
    }

    m->reg_info = NULL;
    m->refcount = 1;
    m->code = code;
    m->pos = 0;
    m->len = 8;
    return m;
}

/* lily_try_new_hash_val
   This attempts to create a new hash value, for storing hash elements.
   Note: 'try' means this call returns NULL on failure. */
lily_hash_val *lily_try_new_hash_val()
{
    lily_hash_val *h = lily_malloc(sizeof(lily_hash_val));

    if (h == NULL)
        return NULL;

    h->refcount = 1;
    h->visited = 0;
    h->num_elems = 0;
    h->elem_chain = NULL;
    return h;
}

/* lily_try_new_hash_elem
   This attempts to create a new hash element for storing a key and a value.
   The caller is responsible for adding this element to a hash value.
   Note: 'try' means this call returns NULL on failure. */
lily_hash_elem *lily_try_new_hash_elem()
{
    lily_hash_elem *elem = lily_malloc(sizeof(lily_hash_elem));

    if (elem == NULL)
        return NULL;

    elem->flags = SYM_IS_NIL;
    elem->value.integer = 0;
    elem->next = NULL;
    return elem;
}

/* lily_try_new_object_val
   This tries to create a new object value.
   Note: 'try' means this call returns NULL on failure. */
lily_object_val *lily_try_new_object_val()
{
    lily_object_val *o = lily_malloc(sizeof(lily_object_val));

    if (o == NULL)
        return NULL;

    o->gc_entry = NULL;
    o->refcount = 1;
    o->sig = NULL;
    o->value.integer = 0;

    return o;
}
