#include "lily_impl.h"
#include "lily_value.h"

#define malloc_mem(size) mem_func(NULL, size)
#define free_mem(ptr)    mem_func(ptr, 0)

/** Deref-ing calls **/

void lily_deref_hash_val(lily_mem_func mem_func, lily_type *type,
        lily_hash_val *hv)
{
    hv->refcount--;
    if (hv->refcount == 0) {
        if (hv->gc_entry != NULL)
            hv->gc_entry->value.generic = NULL;

        lily_type *value_type = type->subtypes[1];
        int value_is_refcounted = value_type->cls->is_refcounted;
        lily_hash_elem *elem, *save_next;
        elem = hv->elem_chain;
        while (elem) {
            lily_value *elem_value = elem->elem_value;
            if ((elem_value->flags & VAL_IS_NIL_OR_PROTECTED) == 0 &&
                value_is_refcounted)
                lily_deref_unknown_val(mem_func, elem_value);

            save_next = elem->next;
            if (elem->elem_key->type->cls->is_refcounted &&
                (elem->elem_key->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                lily_deref_unknown_val(mem_func, elem->elem_key);

            free_mem(elem->elem_key);
            free_mem(elem->elem_value);
            free_mem(elem);
            elem = save_next;
        }

        free_mem(hv);
    }
}

void lily_deref_list_val(lily_mem_func mem_func, lily_type *type,
        lily_list_val *lv)
{
    lv->refcount--;
    if (lv->refcount == 0) {
        /* If this list has a gc entry, then make the value of it NULL. This
           prevents the gc from trying to access the list once it has been
           destroyed. */
        if (lv->gc_entry != NULL)
            lv->gc_entry->value.generic = NULL;

        int i;
        if (type->subtypes[0]->cls->is_refcounted) {
            for (i = 0;i < lv->num_values;i++) {
                if ((lv->elems[i]->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                    lily_deref_unknown_val(mem_func, lv->elems[i]);

                free_mem(lv->elems[i]);
            }
        }
        else {
            for (i = 0;i < lv->num_values;i++)
                free_mem(lv->elems[i]);
        }

        free_mem(lv->elems);
        free_mem(lv);
    }
}

void lily_deref_tuple_val(lily_mem_func mem_func, lily_type *type,
        lily_list_val *tv)
{
    tv->refcount--;
    if (tv->refcount == 0) {
        /* If this tuple has a gc entry, then make the value of it NULL. This
           prevents the gc from trying to access the tuple once it has been
           destroyed. */
        if (tv->gc_entry != NULL)
            tv->gc_entry->value.generic = NULL;

        int i;
        for (i = 0;i < tv->num_values;i++) {
            lily_value *elem_val = tv->elems[i];

            if (elem_val->type->cls->is_refcounted &&
                (elem_val->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                lily_deref_unknown_val(mem_func, elem_val);

            free_mem(elem_val);
        }

        free_mem(tv->elems);
        free_mem(tv);
    }
}

void lily_deref_instance_val(lily_mem_func mem_func, lily_type *type,
        lily_instance_val *iv)
{
    /* Instance values are essentially a tuple but with a class attribute
       tacked on at the end. So use that. */
    lily_deref_tuple_val(mem_func, type, (lily_list_val *)iv);
}

void lily_deref_function_val(lily_mem_func mem_func, lily_function_val *fv)
{
    fv->refcount--;
    if (fv->refcount == 0) {
        if (fv->reg_info != NULL) {
            int i;
            for (i = 0;i < fv->reg_count;i++)
                free_mem(fv->reg_info[i].name);
        }

        free_mem(fv->reg_info);
        free_mem(fv->code);
        free_mem(fv);
    }
}

void lily_deref_string_val(lily_mem_func mem_func, lily_string_val *sv)
{
    sv->refcount--;
    if (sv->refcount == 0) {
        if (sv->string)
            free_mem(sv->string);
        free_mem(sv);
    }
}

void lily_deref_any_val(lily_mem_func mem_func, lily_any_val *av)
{
    av->refcount--;
    if (av->refcount == 0) {
        /* Values of type Any always have a gc entry, so make sure the value of
           it is set to NULL. This prevents the gc from trying to access this
           Any that is about to be destroyed. */
        av->gc_entry->value.generic = NULL;

        if ((av->inner_value->flags & VAL_IS_NIL_OR_PROTECTED) == 0 &&
            av->inner_value->type->cls->is_refcounted)
            lily_deref_unknown_val(mem_func, av->inner_value);

        free_mem(av->inner_value);
        free_mem(av);
    }
}

void lily_deref_package_val(lily_mem_func mem_func, lily_package_val *pv)
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
                lily_deref_unknown_raw_val(mem_func, var_iter->type,
                        var_iter->value);

            free_mem(var_iter->name);
            free_mem(var_iter);
        }

        free_mem(pv->vars);
        free_mem(pv);
    }
}

/*  lily_deref_unknown_val
    This takes a proper value and determines the proper call to deref the given
    value. This is useful if you want to deref something but don't know exactly
    what type it is.

    This should (ideally) not be called if the given value is not refcounted.
    This must never be called with a value that has the nil flag set.

    value: The value to deref. */
void lily_deref_unknown_val(lily_mem_func mem_func, lily_value *value)
{
    lily_raw_value raw = value->value;
    int cls_id = value->type->cls->id;

    if (cls_id == SYM_CLASS_LIST)
        lily_deref_list_val(mem_func, value->type, raw.list);
    else if (cls_id == SYM_CLASS_STRING)
        lily_deref_string_val(mem_func, raw.string);
    else if (cls_id == SYM_CLASS_FUNCTION)
        lily_deref_function_val(mem_func, raw.function);
    else if (cls_id == SYM_CLASS_HASH)
        lily_deref_hash_val(mem_func, value->type, raw.hash);
    else if (value->type->cls->flags & CLS_ENUM_CLASS)
        lily_deref_any_val(mem_func, raw.any);
    else if (cls_id == SYM_CLASS_TUPLE || cls_id >= SYM_CLASS_EXCEPTION)
        lily_deref_tuple_val(mem_func, value->type, raw.list);
    else if (cls_id == SYM_CLASS_PACKAGE)
        lily_deref_package_val(mem_func, raw.package);
}

/*  lily_deref_unknown_raw_value
    This takes a type and a raw value and determines the proper call to deref
    the raw value. This should be thought of as a failsafe in the event that
    a raw_value needs to be destroyed.

    This should (ideally) not be called if the given value is not refcounted.
    This must never be called with a value that has the nil flag set.

    value_type: The type describing the raw value to be deref'd.
    raw:       The raw value to be deref'd. */
void lily_deref_unknown_raw_val(lily_mem_func mem_func, lily_type *value_type,
        lily_raw_value raw)
{
    int cls_id = value_type->cls->id;
    if (cls_id == SYM_CLASS_LIST)
        lily_deref_list_val(mem_func, value_type, raw.list);
    else if (cls_id == SYM_CLASS_STRING)
        lily_deref_string_val(mem_func, raw.string);
    else if (cls_id == SYM_CLASS_FUNCTION)
        lily_deref_function_val(mem_func, raw.function);
    else if (value_type->cls->flags & CLS_ENUM_CLASS)
        lily_deref_any_val(mem_func, raw.any);
    else if (cls_id == SYM_CLASS_HASH)
        lily_deref_hash_val(mem_func, value_type, raw.hash);
    else if (cls_id == SYM_CLASS_TUPLE || cls_id >= SYM_CLASS_EXCEPTION)
        lily_deref_tuple_val(mem_func, value_type, raw.list);
    else if (cls_id == SYM_CLASS_PACKAGE)
        lily_deref_package_val(mem_func, raw.package);
}

/** Value creation calls **/

lily_list_val *lily_new_list_val(lily_mem_func mem_func)
{
    lily_list_val *lv = mem_func(NULL, sizeof(lily_list_val));
    lv->refcount = 1;
    lv->gc_entry = NULL;
    lv->elems = NULL;
    lv->num_values = -1;
    lv->visited = 0;

    return lv;
}

lily_instance_val *lily_new_instance_val(lily_mem_func mem_func)
{
    lily_instance_val *ival = mem_func(NULL, sizeof(lily_instance_val));
    ival->refcount = 1;
    ival->gc_entry = NULL;
    ival->values = NULL;
    ival->num_values = -1;
    ival->visited = 0;
    ival->true_class = NULL;

    return ival;
}

/*  lily_new_foreign_function_val
    Attempt to create a function that will hold a foreign value.

    func:       The call to be invoked when this function gets called.
    class_name: The name of the class that this function belongs to, or NULL.
    name:       The name of the function itself.

    Note: 'try' means this call returns NULL on failure. */
lily_function_val *lily_new_foreign_function_val(lily_mem_func mem_func,
        lily_foreign_func func, char *class_name, char *name)
{
    lily_function_val *f = mem_func(NULL, sizeof(lily_function_val));

    f->refcount = 1;
    f->class_name = class_name;
    f->trace_name = name;
    f->foreign_func = func;
    f->code = NULL;
    f->pos = -1;
    f->len = -1;
    f->reg_info = NULL;
    f->reg_count = -1;
    f->generic_count = 0;
    return f;
}

/*  lily_new_native_function_val
    Attempt to create a function that will hold native code.

    class_name: The name of the class that this function belongs to, or NULL.
    name:       The name of this function.

    Note: 'try' means this call returns NULL on failure. */
lily_function_val *lily_new_native_function_val(lily_mem_func mem_func,
        char *class_name, char *name)
{
    lily_function_val *f = malloc_mem(sizeof(lily_function_val));
    uint16_t *code = malloc_mem(8 * sizeof(uint16_t));

    f->refcount = 1;
    f->class_name = class_name;
    f->trace_name = name;
    f->foreign_func = NULL;
    f->code = code;
    f->pos = 0;
    f->len = 8;
    f->reg_info = NULL;
    f->reg_count = -1;
    f->generic_count = 0;
    return f;
}

/* lily_new_hash_val
   This attempts to create a new hash value, for storing hash elements.
   Note: 'try' means this call returns NULL on failure. */
lily_hash_val *lily_new_hash_val(lily_mem_func mem_func)
{
    lily_hash_val *h = malloc_mem(sizeof(lily_hash_val));

    h->gc_entry = NULL;
    h->refcount = 1;
    h->visited = 0;
    h->num_elems = 0;
    h->elem_chain = NULL;
    return h;
}

/* lily_new_hash_elem
   This attempts to create a new hash element for storing a key and a value.
   The caller is responsible for adding this element to a hash value.
   Note: 'try' means this call returns NULL on failure. */
lily_hash_elem *lily_new_hash_elem(lily_mem_func mem_func)
{
    lily_hash_elem *elem = mem_func(NULL, sizeof(lily_hash_elem));

    elem->elem_key = malloc_mem(sizeof(lily_value));
    elem->elem_value = malloc_mem(sizeof(lily_value));

    /* Hash lookup does not take into account or allow nil keys. So this should
       be set to a non-nil value as soon as possible. */
    elem->elem_key->flags = VAL_IS_NIL;
    elem->elem_key->value.integer = 0;

    elem->elem_value->flags = VAL_IS_NIL;
    elem->elem_value->value.integer = 0;

    elem->next = NULL;
    return elem;
}

/* lily_new_any_val
   This tries to create a new "any" value.
   Note: 'try' means this call returns NULL on failure. */
lily_any_val *lily_new_any_val(lily_mem_func mem_func)
{
    lily_any_val *a = malloc_mem(sizeof(lily_any_val));

    a->inner_value = malloc_mem(sizeof(lily_value));
    a->inner_value->flags = VAL_IS_NIL;
    a->inner_value->type = NULL;
    a->inner_value->value.integer = 0;
    a->gc_entry = NULL;
    a->refcount = 1;

    return a;
}
