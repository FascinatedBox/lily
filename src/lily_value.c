#include "lily_impl.h"
#include "lily_value.h"

/** Deref-ing calls **/

void lily_deref_hash_val(lily_sig *sig, lily_hash_val *hv)
{
    hv->refcount--;
    if (hv->refcount == 0) {
        if (hv->gc_entry != NULL)
            hv->gc_entry->value.generic = NULL;

        lily_sig *value_sig = sig->siglist[1];
        int value_cls_id = value_sig->cls->id;
        int value_is_refcounted = value_sig->cls->is_refcounted;
        lily_hash_elem *elem, *save_next;
        elem = hv->elem_chain;
        while (elem) {
            lily_value *elem_value = elem->elem_value;
            if ((elem_value->flags & VAL_IS_NIL_OR_PROTECTED) == 0 &&
                value_is_refcounted)
                lily_deref_unknown_val(elem_value);
            else if (value_cls_id == SYM_CLASS_OBJECT) {
                /* Objects are containers that are not shared. This circularity
                   applies to what's inside the object, not the object itself.
                   Make sure to destroy the object. */
                lily_object_val *ov = elem_value->value.object;
                lily_free(ov);
            }

            save_next = elem->next;
            if (elem->elem_key->sig->cls->is_refcounted &&
                (elem->elem_key->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                lily_deref_unknown_val(elem->elem_key);

            lily_free(elem->elem_key);
            lily_free(elem->elem_value);
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

        int i;
        if (sig->siglist[0]->cls->is_refcounted) {
            for (i = 0;i < lv->num_values;i++) {
                if ((lv->elems[i]->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                    lily_deref_unknown_val(lv->elems[i]);

                lily_free(lv->elems[i]);
            }
        }
        else {
            for (i = 0;i < lv->num_values;i++)
                lily_free(lv->elems[i]);
        }

        lily_free(lv->elems);
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

        if ((ov->inner_value->flags & VAL_IS_NIL_OR_PROTECTED) == 0 &&
            ov->inner_value->sig->cls->is_refcounted)
            lily_deref_unknown_val(ov->inner_value);

        lily_free(ov->inner_value);
        lily_free(ov);
    }
}

void lily_deref_package_val(lily_package_val *pv)
{
    pv->refcount--;
    if (pv->refcount == 0) {
        if (pv->gc_entry)
            pv->gc_entry->value.generic = NULL;

        int i;
        /* Packages take ownership of the vars they hold from the symtab.
           Therefore, they are responsible for destroying the vars held. */
        for (i = 0;i < pv->var_count;i++) {
            lily_var *var_iter = pv->vars[i];
            if ((var_iter->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                lily_deref_unknown_raw_val(var_iter->sig, var_iter->value);

            lily_free(var_iter->name);
            lily_free(var_iter);
        }

        lily_free(pv->vars);
        lily_free(pv);
    }
}

/*  lily_deref_unknown_val
    This takes a proper value and determines the proper call to deref the given
    value. This is useful if you want to deref something but don't know exactly
    what type it is.

    This should (ideally) not be called if the given value is not refcounted.
    This must never be called with a value that has the nil flag set.

    value: The value to deref. */
void lily_deref_unknown_val(lily_value *value)
{
    lily_raw_value raw = value->value;
    int cls_id = value->sig->cls->id;

    if (cls_id == SYM_CLASS_LIST)
        lily_deref_list_val(value->sig, raw.list);
    else if (cls_id == SYM_CLASS_STR)
        lily_deref_str_val(raw.str);
    else if (cls_id == SYM_CLASS_METHOD)
        lily_deref_method_val(raw.method);
    else if (cls_id == SYM_CLASS_OBJECT)
        lily_deref_object_val(raw.object);
    else if (cls_id == SYM_CLASS_HASH)
        lily_deref_hash_val(value->sig, raw.hash);
    else if (cls_id == SYM_CLASS_PACKAGE)
        lily_deref_package_val(raw.package);
}

/*  lily_deref_unknown_raw_value
    This takes a sig and a raw value and determines the proper call to deref
    the raw value. This should be thought of as a failsafe in the event that
    a raw_value needs to be destroyed.

    This should (ideally) not be called if the given value is not refcounted.
    This must never be called with a value that has the nil flag set.

    value_sig: The signature describing the raw value to be deref'd.
    raw:       The raw value to be deref'd. */
void lily_deref_unknown_raw_val(lily_sig *value_sig, lily_raw_value raw)
{
    int cls_id = value_sig->cls->id;
    if (cls_id == SYM_CLASS_LIST)
        lily_deref_list_val(value_sig, raw.list);
    else if (cls_id == SYM_CLASS_STR)
        lily_deref_str_val(raw.str);
    else if (cls_id == SYM_CLASS_METHOD)
        lily_deref_method_val(raw.method);
    else if (cls_id == SYM_CLASS_OBJECT)
        lily_deref_object_val(raw.object);
    else if (cls_id == SYM_CLASS_HASH)
        lily_deref_hash_val(value_sig, raw.hash);
    else if (cls_id == SYM_CLASS_PACKAGE)
        lily_deref_package_val(raw.package);
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

    h->gc_entry = NULL;
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

    elem->elem_key = lily_malloc(sizeof(lily_value));
    elem->elem_value = lily_malloc(sizeof(lily_value));
    if (elem->elem_key == NULL || elem->elem_value == NULL) {
        lily_free(elem->elem_key);
        lily_free(elem->elem_value);
        lily_free(elem);
        return NULL;
    }

    /* Hash lookup does not take into account or allow nil keys. So this should
       be set to a non-nil value as soon as possible. */
    elem->elem_key->flags = VAL_IS_NIL;
    elem->elem_key->value.integer = 0;

    elem->elem_value->flags = VAL_IS_NIL;
    elem->elem_value->value.integer = 0;

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

    o->inner_value = lily_malloc(sizeof(lily_value));
    if (o->inner_value == NULL) {
        lily_free(o);
        return NULL;
    }

    o->inner_value->flags = VAL_IS_NIL;
    o->inner_value->sig = NULL;
    o->inner_value->value.integer = 0;
    o->gc_entry = NULL;
    o->refcount = 1;

    return o;
}
