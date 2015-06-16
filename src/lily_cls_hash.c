#include "lily_alloc.h"
#include "lily_vm.h"
#include "lily_value.h"
#include "lily_seed.h"

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

int lily_hash_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    if (*depth == 100)
        lily_raise(vm->raiser, lily_RecursionError, "Infinite loop in comparison.\n");

    int ret;

    if (left->value.hash->num_elems == right->value.hash->num_elems) {
        lily_hash_val *left_hash = left->value.hash;
        lily_hash_val *right_hash = right->value.hash;

        class_eq_func key_eq_func = left->type->subtypes[0]->cls->eq_func;
        class_eq_func value_eq_func = left->type->subtypes[1]->cls->eq_func;

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
    else
        ret = 0;

    return ret;
}

void lily_gc_hash_marker(int pass, lily_value *v)
{
    lily_hash_val *hash_val = v->value.hash;
    if (hash_val->gc_entry &&
        hash_val->gc_entry->last_pass != pass) {
        hash_val->gc_entry->last_pass = pass;

        lily_type *hash_value_type = v->type->subtypes[1];
        void (*gc_marker)(int, lily_value *);

        gc_marker = hash_value_type->cls->gc_marker;

        lily_hash_elem *elem_iter = hash_val->elem_chain;
        while (elem_iter) {
            lily_value *elem_value = elem_iter->elem_value;
            if ((elem_value->flags & VAL_IS_NIL) == 0)
                gc_marker(pass, elem_value);

            elem_iter = elem_iter->next;
        }
    }
}

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

void lily_gc_collect_hash(lily_type *hash_type,
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
            if ((elem_key->flags & VAL_IS_NOT_DEREFABLE) == 0) {
                lily_raw_value k = elem_key->value;
                if (k.generic->refcount == 1)
                    lily_gc_collect_value(hash_key_type, k);
                else
                    k.generic->refcount--;
            }

            if ((elem_value->flags & VAL_IS_NOT_DEREFABLE) == 0) {
                lily_raw_value v = elem_value->value;
                if (v.generic->refcount == 1)
                    lily_gc_collect_value(hash_value_type, v);
                else
                    v.generic->refcount--;
            }

            lily_free(elem_iter->elem_key);
            lily_free(elem_iter->elem_value);
            lily_free(elem_iter);
            elem_iter = elem_temp;
        }

        if (marked == 0)
            lily_free(hash_val);
    }
}

void lily_hash_get(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input = vm_regs[code[0]];
    lily_value *find_key = vm_regs[code[1]];
    lily_value *default_value = vm_regs[code[2]];
    lily_value *result = vm_regs[code[3]];

    uint64_t siphash = lily_calculate_siphash(vm->sipkey, find_key);
    lily_hash_elem *hash_elem = lily_lookup_hash_elem(input->value.hash,
            siphash, find_key);

    lily_value *new_value;
    if (hash_elem)
        new_value = hash_elem->elem_value;
    else
        new_value = default_value;

    lily_assign_value(vm, result, new_value);
}

/*  lily_hash_keys
    Implements hash::keys

    This function returns a list containing each key within the hash. */
void lily_hash_keys(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[0]]->value.hash;
    lily_value *result_reg = vm_regs[code[1]];

    int num_elems = hash_val->num_elems;

    lily_list_val *result_lv = lily_malloc(sizeof(lily_list_val));
    result_lv->num_values = num_elems;
    result_lv->visited = 0;
    result_lv->refcount = 1;
    result_lv->elems = lily_malloc(num_elems * sizeof(lily_value *));
    result_lv->gc_entry = NULL;

    lily_type *key_type = result_reg->type->subtypes[0];
    int i = 0;

    lily_hash_elem *elem_iter = hash_val->elem_chain;
    while (elem_iter) {
        lily_value *new_value = lily_malloc(sizeof(lily_value));
        new_value->type = key_type;
        new_value->value.integer = 0;
        new_value->flags = VAL_IS_NIL;

        lily_assign_value(vm, new_value, elem_iter->elem_key);
        result_lv->elems[i] = new_value;

        i++;
        elem_iter = elem_iter->next;
    }

    lily_deref(result_reg);

    result_reg->value.list = result_lv;
    result_reg->flags = 0;
}

static const lily_func_seed keys =
    {NULL, "keys", dyna_function, "function keys[A, B](hash[A, B] => list[A])", lily_hash_keys};

static const lily_func_seed dynaload_start =
    {&keys, "get", dyna_function, "function get[A, B](hash[A, B], A, B => B)", lily_hash_get};

static const lily_class_seed hash_seed =
{
    NULL,                 /* next */
    "hash",               /* name */
    dyna_class,           /* load_type */
    1,                    /* is_refcounted */
    2,                    /* generic_count */
    0,                    /* flags */
    &dynaload_start,      /* dynaload_table */
    &lily_gc_hash_marker, /* gc_marker */
    &lily_hash_eq,        /* eq_func */
    lily_destroy_hash,    /* destroy_func */
};

lily_class *lily_hash_init(lily_symtab *symtab)
{
    return lily_new_class_by_seed(symtab, &hash_seed);
}
