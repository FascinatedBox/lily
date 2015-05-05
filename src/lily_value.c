#include "lily_value.h"
#include "lily_alloc.h"

/*  lily_deref
    This function will check that the value is refcounted and that it is not
    nil/protected before giving it a deref. It is therefore safe to pass
    anything to this function as long as it's not a NULL value.
    If the value given falls to 0 refs, it is immediately destroyed, as well as
    whatever is inside of it.

    Note: This destroys the contents inside the value, NOT the value itself. */
void lily_deref(lily_value *value)
{
    lily_class *cls = value->type->cls;

    if (cls->is_refcounted &&
        (value->flags & VAL_IS_NIL_OR_PROTECTED) == 0) {

        value->value.generic->refcount--;
        if (value->value.generic->refcount == 0)
            value->type->cls->destroy_func(value);
    }
}

/*  lily_deref_raw
    This is a helper function for lily_deref. This function calls lily_deref
    with a proper value that has the given type and raw value inside. */
void lily_deref_raw(lily_type *type, lily_raw_value raw)
{
    lily_value v;
    v.type = type;
    v.flags = 0;
    v.value = raw;

    lily_deref(&v);
}

/** Deref-ing calls **/

void lily_destroy_hash(lily_value *v)
{
    lily_hash_val *hv = v->value.hash;

    if (hv->gc_entry != NULL)
        hv->gc_entry->value.generic = NULL;

    lily_hash_elem *elem, *save_next;
    elem = hv->elem_chain;
    while (elem) {
        lily_value *elem_value = elem->elem_value;

        lily_deref(elem_value);

        save_next = elem->next;

        lily_deref(elem->elem_key);

        lily_free(elem->elem_key);
        lily_free(elem->elem_value);
        lily_free(elem);
        elem = save_next;
    }

    lily_free(hv);
}

void lily_destroy_list(lily_value *v)
{
    lily_type *type = v->type;
    lily_list_val *lv = v->value.list;

    /* If this list has a gc entry, then make the value of it NULL. This
        prevents the gc from trying to access the list once it has been
        destroyed. */
    if (lv->gc_entry != NULL)
        lv->gc_entry->value.generic = NULL;

    int i;
    if (type->subtypes[0]->cls->is_refcounted) {
        for (i = 0;i < lv->num_values;i++) {
            lily_deref(lv->elems[i]);

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

void lily_destroy_instance(lily_value *v)
{
    /* Instance values are essentially a tuple but with a class attribute
       tacked on at the end. So use that. */
    lily_destroy_tuple(v);
}

void lily_destroy_function(lily_value *v)
{
    lily_function_val *fv = v->value.function;

    if (fv->reg_info != NULL) {
        int i;
        for (i = 0;i < fv->reg_count;i++)
            lily_free(fv->reg_info[i].name);
    }

    lily_free(fv->reg_info);
    lily_free(fv->code);
    lily_free(fv);
}

void lily_destroy_string(lily_value *v)
{
    lily_string_val *sv = v->value.string;

    if (sv->string)
        lily_free(sv->string);

    lily_free(sv);
}

void lily_destroy_symbol(lily_value *v)
{
    lily_symbol_val *symv = v->value.symbol;

    if (symv->has_literal)
        /* Keep the refcount at one so that the symtab can use this function
            to free symbols at exit (by stripping away the literal). */
        symv->refcount++;
    else {
        /* Since this symbol has no literal associated with it, it exists only
           in vm space and it can die.
           But first, make sure the symtab's entry has that spot blanked out to
           prevent an invalid read when looking over symbols associated with
           entries. */
        symv->entry->symbol = NULL;
        lily_free(symv->string);
        lily_free(symv);
    }
}

void lily_destroy_any(lily_value *v)
{
    lily_any_val *av = v->value.any;

    /* Values of type 'any' always have a gc entry, so make sure the value of it
       is set to NULL. This prevents the gc from trying to access this 'any'
       that is about to be destroyed. */
    av->gc_entry->value.generic = NULL;

    lily_deref(av->inner_value);

    lily_free(av->inner_value);
    lily_free(av);
}

void lily_destroy_file(lily_value *v)
{
    lily_file_val *filev = v->value.file;

    if (filev->is_open)
        fclose(filev->inner_file);

    lily_free(filev);
}

/** Value creation calls **/

lily_list_val *lily_new_list_val()
{
    lily_list_val *lv = lily_malloc(sizeof(lily_list_val));
    lv->refcount = 1;
    lv->gc_entry = NULL;
    lv->elems = NULL;
    lv->num_values = -1;
    lv->visited = 0;

    return lv;
}

lily_instance_val *lily_new_instance_val()
{
    lily_instance_val *ival = lily_malloc(sizeof(lily_instance_val));
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
lily_function_val *lily_new_foreign_function_val(
        lily_foreign_func func, char *class_name, char *name)
{
    lily_function_val *f = lily_malloc(sizeof(lily_function_val));

    f->refcount = 1;
    f->class_name = class_name;
    f->trace_name = name;
    f->foreign_func = func;
    f->code = NULL;
    f->pos = -1;
    f->len = -1;
    f->reg_info = NULL;
    f->reg_count = -1;
    f->generic_pos = 0;
    return f;
}

/*  lily_new_native_function_val
    Attempt to create a function that will hold native code.

    class_name: The name of the class that this function belongs to, or NULL.
    name:       The name of this function.

    Note: 'try' means this call returns NULL on failure. */
lily_function_val *lily_new_native_function_val(
        char *class_name, char *name)
{
    lily_function_val *f = lily_malloc(sizeof(lily_function_val));
    uint16_t *code = lily_malloc(8 * sizeof(uint16_t));

    f->refcount = 1;
    f->class_name = class_name;
    f->trace_name = name;
    f->foreign_func = NULL;
    f->code = code;
    f->pos = 0;
    f->len = 8;
    f->reg_info = NULL;
    f->reg_count = -1;
    f->generic_pos = 0;
    return f;
}

/* lily_new_hash_val
   This attempts to create a new hash value, for storing hash elements.
   Note: 'try' means this call returns NULL on failure. */
lily_hash_val *lily_new_hash_val()
{
    lily_hash_val *h = lily_malloc(sizeof(lily_hash_val));

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
lily_hash_elem *lily_new_hash_elem()
{
    lily_hash_elem *elem = lily_malloc(sizeof(lily_hash_elem));

    elem->elem_key = lily_malloc(sizeof(lily_value));
    elem->elem_value = lily_malloc(sizeof(lily_value));

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
lily_any_val *lily_new_any_val()
{
    lily_any_val *a = lily_malloc(sizeof(lily_any_val));

    a->inner_value = lily_malloc(sizeof(lily_value));
    a->inner_value->flags = VAL_IS_NIL;
    a->inner_value->type = NULL;
    a->inner_value->value.integer = 0;
    a->gc_entry = NULL;
    a->refcount = 1;

    return a;
}

lily_file_val *lily_new_file_val(FILE *inner_file,
        char mode_ch)
{
    lily_file_val *filev = lily_malloc(sizeof(lily_file_val));

    filev->refcount = 1;
    filev->inner_file = inner_file;
    filev->is_open = (inner_file != NULL);

    int read_ok = 1;
    int write_ok = 1;

    if (mode_ch == 'w')
        read_ok = 0;
    else if (mode_ch == 'r')
        write_ok = 0;

    filev->read_ok = read_ok;
    filev->write_ok = write_ok;

    return filev;
}
