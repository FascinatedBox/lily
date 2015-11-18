#include <string.h>

#include "lily_alloc.h"
#include "lily_vm.h"
#include "lily_value.h"
#include "lily_seed.h"
#include "lily_cls_list.h"

lily_hash_val *lily_new_hash_val()
{
    lily_hash_val *h = lily_malloc(sizeof(lily_hash_val));

    h->gc_entry = NULL;
    h->refcount = 1;
    h->iter_count = 0;
    h->visited = 0;
    h->num_elems = 0;
    h->elem_chain = NULL;
    return h;
}

/*  lily_hash_get_elem

    Attempt to find 'key' within 'hash_val'. If an element is found, then it is
    returned. If no element is found, then NULL is returned. */
lily_hash_elem *lily_hash_get_elem(lily_vm_state *vm, lily_hash_val *hash_val,
        lily_value *key)
{
    uint64_t key_siphash = lily_siphash(vm, key);
    lily_hash_elem *elem_iter = hash_val->elem_chain;
    lily_raw_value key_value = key->value;
    int key_cls_id = key->type->cls->id;
    int ok = 0;

    while (elem_iter) {
        if (elem_iter->key_siphash == key_siphash) {
            lily_raw_value iter_value = elem_iter->elem_key->value;

            if (key_cls_id == SYM_CLASS_INTEGER &&
                iter_value.integer == key_value.integer)
                ok = 1;
            else if (key_cls_id == SYM_CLASS_DOUBLE &&
                     iter_value.doubleval == key_value.doubleval)
                ok = 1;
            else if (key_cls_id == SYM_CLASS_STRING &&
                    /* strings are immutable, so try a ptr compare first. */
                    ((iter_value.string == key_value.string) ||
                     /* No? Make sure the sizes match, then call for a strcmp.
                        The size check is an easy way to potentially skip a
                        strcmp in case of hash collision. */
                      (iter_value.string->size == key_value.string->size &&
                       strcmp(iter_value.string->string,
                              key_value.string->string) == 0)))
                ok = 1;
            else
                ok = 0;

            if (ok)
                break;
        }
        elem_iter = elem_iter->next;
    }

    return elem_iter;
}

static inline void remove_key_check(lily_vm_state *vm, lily_hash_val *hash_val)
{
    if (hash_val->iter_count)
        lily_raise(vm->raiser, lily_RuntimeError,
                "Cannot remove key from hash during iteration.\n");
}

/*  hash_add_unique_nocopy

    This adds a new element to the hash, with 'pair_key' and 'pair_value'
    inside. The key and value are not given a refbump, and are not copied over.
    For that, see lily_hash_add_unique. */
static void hash_add_unique_nocopy(lily_vm_state *vm, lily_hash_val *hash_val,
        lily_value *pair_key, lily_value *pair_value)
{
    remove_key_check(vm, hash_val);

    lily_hash_elem *elem = lily_malloc(sizeof(lily_hash_elem));

    elem->key_siphash = lily_siphash(vm, pair_key);
    elem->elem_key = pair_key;
    elem->elem_value = pair_value;

    if (hash_val->elem_chain)
        hash_val->elem_chain->prev = elem;

    elem->prev = NULL;
    elem->next = hash_val->elem_chain;
    hash_val->elem_chain = elem;

    hash_val->num_elems++;
}

/*  lily_hash_add_unique

    This function will add an element to the hash with 'pair_key' as the key and
    'pair_value' as the value. This should only be used in cases where the
    caller is completely certain that 'pair_key' is not within the hash. If the
    caller is unsure, then lily_hash_set_elem should be used instead. */
void lily_hash_add_unique(lily_vm_state *vm, lily_hash_val *hash_val,
        lily_value *pair_key, lily_value *pair_value)
{
    pair_key = lily_copy_value(pair_key);
    pair_value = lily_copy_value(pair_value);

    hash_add_unique_nocopy(vm, hash_val, pair_key, pair_value);
}

/*  lily_hash_set_elem

    This attempts to find 'pair_key' within 'hash_val'. If successful, then
    the element's value is assigned to 'pair_value'. If unable to find an
    element, a new element is created using 'pair_key' and 'pair_value'. */
void lily_hash_set_elem(lily_vm_state *vm, lily_hash_val *hash_val,
        lily_value *pair_key, lily_value *pair_value)
{
    lily_hash_elem *elem = lily_hash_get_elem(vm, hash_val, pair_key);
    if (elem == NULL)
        lily_hash_add_unique(vm, hash_val, pair_key, pair_value);
    else
        lily_assign_value(elem->elem_value, pair_value);
}

int lily_hash_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    if (*depth == 100)
        lily_raise(vm->raiser, lily_RuntimeError, "Infinite loop in comparison.\n");

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

static void destroy_elem(lily_hash_elem *elem)
{
    lily_deref(elem->elem_key);
    lily_free(elem->elem_key);

    lily_deref(elem->elem_value);
    lily_free(elem->elem_value);

    lily_free(elem);
}

static void destroy_hash_elems(lily_hash_val *hash_val)
{
    lily_hash_elem *elem_iter = hash_val->elem_chain;
    lily_hash_elem *elem_next;

    while (elem_iter) {
        elem_next = elem_iter->next;

        destroy_elem(elem_iter);

        elem_iter = elem_next;
    }
}

void lily_destroy_hash(lily_value *v)
{
    lily_hash_val *hv = v->value.hash;

    if (hv->gc_entry != NULL)
        hv->gc_entry->value.generic = NULL;

    destroy_hash_elems(hv);

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

void lily_hash_clear(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;

    if (hash_val->iter_count != 0)
        lily_raise(vm->raiser, lily_RuntimeError,
                "Cannot remove key from hash during iteration.\n");

    destroy_hash_elems(hash_val);

    hash_val->elem_chain = NULL;
    hash_val->num_elems = 0;
}

void lily_hash_get(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input = vm_regs[code[1]];
    lily_value *key = vm_regs[code[2]];
    lily_value *default_value = vm_regs[code[3]];
    lily_value *result = vm_regs[code[0]];

    lily_hash_elem *hash_elem = lily_hash_get_elem(vm, input->value.hash, key);
    lily_value *new_value = hash_elem ? hash_elem->elem_value : default_value;

    lily_assign_value(result, new_value);
}

/*  lily_hash_keys
    Implements hash::keys

    This function returns a list containing each key within the hash. */
void lily_hash_keys(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;
    lily_value *result_reg = vm_regs[code[0]];

    int num_elems = hash_val->num_elems;

    lily_list_val *result_lv = lily_new_list_val();
    result_lv->num_values = num_elems;
    result_lv->elems = lily_malloc(num_elems * sizeof(lily_value *));

    int i = 0;

    lily_hash_elem *elem_iter = hash_val->elem_chain;
    while (elem_iter) {
        result_lv->elems[i] = lily_copy_value(elem_iter->elem_key);

        i++;
        elem_iter = elem_iter->next;
    }

    lily_deref(result_reg);

    result_reg->value.list = result_lv;
    result_reg->flags = 0;
}

void lily_hash_delete(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;
    lily_value *key = vm_regs[code[2]];

    remove_key_check(vm, hash_val);

    lily_hash_elem *hash_elem = lily_hash_get_elem(vm, hash_val, key);

    if (hash_elem) {
        if (hash_elem->next)
            hash_elem->next->prev = hash_elem->prev;

        if (hash_elem->prev)
            hash_elem->prev->next = hash_elem->next;

        if (hash_elem == hash_val->elem_chain)
            hash_val->elem_chain = hash_elem->next;

        destroy_elem(hash_elem);
        hash_val->num_elems--;
    }
}

/*  Implements hash::each_pair[A, B](hash[A, B], function(A, B))

    This is fairly simple: It takes a function that takes both the key and the
    value of a hash and calls it for each entry of the hash. */
void lily_hash_each_pair(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;
    lily_value *function_reg = vm_regs[code[2]];
    lily_hash_elem *elem_iter = hash_val->elem_chain;
    int cached = 0;

    hash_val->iter_count++;
    lily_jump_link *link = lily_jump_setup(vm->raiser);
    if (setjmp(link->jump) == 0) {
        while (elem_iter) {
            lily_value *e_key = elem_iter->elem_key;
            lily_value *e_value = elem_iter->elem_value;

            lily_foreign_call(vm, &cached, NULL, function_reg, 2, e_key,
                    e_value);

            elem_iter = elem_iter->next;
        }

        hash_val->iter_count--;
        lily_release_jump(vm->raiser);
    }
    else {
        hash_val->iter_count--;
        lily_jump_back(vm->raiser);
    }
}

void lily_hash_has_key(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;
    lily_value *key = vm_regs[code[2]];

    lily_hash_elem *hash_elem = lily_hash_get_elem(vm, hash_val, key);

    lily_raw_value v = {.integer = hash_elem != NULL};
    lily_move_raw_value(vm_regs[code[0]], v);
}

static void build_hash_from_vm_list(lily_vm_state *vm, int start,
        lily_value *result_reg)
{
    int stop = vm->vm_list->pos;
    int i;
    lily_hash_val *hash_val = lily_new_hash_val();
    lily_value **values = vm->vm_list->values;

    if (result_reg->type->flags & TYPE_MAYBE_CIRCULAR)
        lily_add_gc_item(vm, result_reg->type,
                (lily_generic_gc_val *)hash_val);

    for (i = start;i < stop;i += 2) {
        lily_value *e_key = values[i];
        lily_value *e_value = values[i + 1];

        hash_add_unique_nocopy(vm, hash_val, e_key, e_value);
    }

    vm->vm_list->pos = start;

    lily_raw_value v = {.hash = hash_val};
    lily_move_raw_value(result_reg, v);
}

void lily_hash_map_values(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;
    lily_value *function_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];

    lily_type *expect_type = function_reg->type->subtypes[0];
    lily_hash_elem *elem_iter = hash_val->elem_chain;
    lily_vm_list *vm_list = vm->vm_list;
    int cached = 0;
    int vm_list_start = vm->vm_list->pos;

    lily_vm_list_ensure(vm, hash_val->num_elems * 2);

    hash_val->iter_count++;
    lily_jump_link *link = lily_jump_setup(vm->raiser);

    if (setjmp(link->jump) == 0) {
        while (elem_iter) {
            lily_value *e_value = elem_iter->elem_value;

            lily_value *new_value = lily_foreign_call(vm, &cached, expect_type,
                    function_reg, 1, e_value);

            vm_list->values[vm_list->pos] = lily_copy_value(elem_iter->elem_key);
            vm_list->values[vm_list->pos+1] = lily_copy_value(new_value);
            vm_list->pos += 2;

            elem_iter = elem_iter->next;
        }

        build_hash_from_vm_list(vm, vm_list_start, result_reg);
        hash_val->iter_count--;
        lily_release_jump(vm->raiser);
    }
    else {
        hash_val->iter_count--;
        lily_jump_back(vm->raiser);
    }
}

void lily_hash_size(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;

    lily_raw_value v = {.integer = hash_val->num_elems};
    lily_move_raw_value(vm_regs[code[0]], v);
}

static const lily_func_seed clear =
    {NULL, "clear", dyna_function, "[A, B](hash[A, B])", lily_hash_clear};

static const lily_func_seed delete_fn =
    {&clear, "delete", dyna_function, "[A, B](hash[A, B], A)", lily_hash_delete};

static const lily_func_seed each_pair =
    {&delete_fn, "each_pair", dyna_function, "[A, B](hash[A, B], function(A, B))", lily_hash_each_pair};

static const lily_func_seed has_key =
    {&each_pair, "has_key", dyna_function, "[A, B](hash[A, B], A):boolean", lily_hash_has_key};

static const lily_func_seed keys =
    {&has_key, "keys", dyna_function, "[A, B](hash[A, B]):list[A]", lily_hash_keys};

static const lily_func_seed get =
    {&keys, "get", dyna_function, "[A, B](hash[A, B], A, B):B", lily_hash_get};

static const lily_func_seed map_values =
    {&get, "map_values", dyna_function, "[A, B, C](hash[A, B], function(B => C)): hash[A, C]", lily_hash_map_values};

static const lily_func_seed dynaload_start =
    {&map_values, "size", dyna_function, "[A, B](hash[A, B]):integer", lily_hash_size};

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
