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
    h->num_elems = 0;
    h->elem_chain = NULL;
    return h;
}

/* Attempt to find 'key' within 'hash_val'. If an element is found, then it is
   returned. If no element is found, then NULL is returned. */
lily_hash_elem *lily_hash_get_elem(lily_vm_state *vm, lily_hash_val *hash_val,
        lily_value *key)
{
    uint64_t key_siphash = lily_siphash(vm, key);
    lily_hash_elem *elem_iter = hash_val->elem_chain;
    lily_raw_value key_value = key->value;
    int flags = key->flags;
    int ok = 0;

    while (elem_iter) {
        if (elem_iter->key_siphash == key_siphash) {
            lily_raw_value iter_value = elem_iter->elem_key->value;

            if (flags & VAL_IS_INTEGER &&
                iter_value.integer == key_value.integer)
                ok = 1;
            else if (flags & VAL_IS_DOUBLE &&
                     iter_value.doubleval == key_value.doubleval)
                ok = 1;
            else if (flags & VAL_IS_STRING &&
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

/* This adds a new element to the hash, with 'pair_key' and 'pair_value' inside.
   The key and value are not given a refbump, and are not copied over. For that,
   see lily_hash_add_unique. */
static void hash_add_unique_nocopy(lily_vm_state *vm, lily_hash_val *hash_val,
        lily_value *pair_key, lily_value *pair_value)
{
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

/* This function will add an element to the hash with 'pair_key' as the key and
   'pair_value' as the value. This should only be used in cases where the
   caller is completely certain that 'pair_key' is not within the hash. If the
   caller is unsure, then lily_hash_set_elem should be used instead. */
void lily_hash_add_unique(lily_vm_state *vm, lily_hash_val *hash_val,
        lily_value *pair_key, lily_value *pair_value)
{
    remove_key_check(vm, hash_val);

    pair_key = lily_copy_value(pair_key);
    pair_value = lily_copy_value(pair_value);

    hash_add_unique_nocopy(vm, hash_val, pair_key, pair_value);
}

/* This attempts to find 'pair_key' within 'hash_val'. If successful, then the
   element's value is assigned to 'pair_value'. If unable to find an element, a
   new element is created using 'pair_key' and 'pair_value'. */
void lily_hash_set_elem(lily_vm_state *vm, lily_hash_val *hash_val,
        lily_value *pair_key, lily_value *pair_value)
{
    lily_hash_elem *elem = lily_hash_get_elem(vm, hash_val, pair_key);
    if (elem == NULL)
        lily_hash_add_unique(vm, hash_val, pair_key, pair_value);
    else
        lily_assign_value(elem->elem_value, pair_value);
}

void lily_gc_hash_marker(int pass, lily_value *v)
{
    lily_hash_val *hash_val = v->value.hash;
    lily_hash_elem *elem_iter = hash_val->elem_chain;
    while (elem_iter) {
        lily_value *elem_value = elem_iter->elem_value;
        lily_gc_mark(pass, elem_value);

        elem_iter = elem_iter->next;
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

void lily_gc_collect_hash(lily_value *v)
{
    lily_hash_val *hash_val = v->value.hash;
    int marked = 0;
    if (hash_val->gc_entry == NULL ||
        (hash_val->gc_entry->last_pass != -1 &&
         hash_val->gc_entry->value.generic != NULL)) {

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
            if (elem_key->flags & VAL_IS_DEREFABLE) {
                lily_raw_value k = elem_key->value;
                if (k.generic->refcount == 1)
                    lily_gc_collect_value(elem_key);
                else
                    k.generic->refcount--;
            }

            if (elem_value->flags & VAL_IS_DEREFABLE) {
                lily_raw_value v = elem_value->value;
                if (v.generic->refcount == 1)
                    lily_gc_collect_value(elem_value);
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

    lily_move_list(result_reg, result_lv);
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

            lily_foreign_call(vm, &cached, 0, function_reg, 2, e_key,
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

    lily_move_integer(vm_regs[code[0]], hash_elem != NULL);
}

static void build_hash_from_vm_list(lily_vm_state *vm, int start,
        lily_value *result_reg)
{
    int stop = vm->vm_list->pos;
    int i;
    lily_hash_val *hash_val = lily_new_hash_val();
    lily_value **values = vm->vm_list->values;

    for (i = start;i < stop;i += 2) {
        lily_value *e_key = values[i];
        lily_value *e_value = values[i + 1];

        hash_add_unique_nocopy(vm, hash_val, e_key, e_value);
    }

    vm->vm_list->pos = start;

    lily_move_hash(result_reg, hash_val);
    lily_tag_value(vm, result_reg);
}

void lily_hash_map_values(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;
    lily_value *function_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];

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

            lily_value *new_value = lily_foreign_call(vm, &cached, 1,
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

void lily_hash_merge(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;
    lily_list_val *to_merge = vm_regs[code[2]]->value.list;
    lily_value *result_reg = vm_regs[code[0]];

    lily_hash_val *result_hash = lily_new_hash_val();

    /* The existing hash should be entirely unique, so just add the pairs in
       directly. */
    lily_hash_elem *elem_iter = hash_val->elem_chain;
    while (elem_iter) {
        lily_hash_add_unique(vm, result_hash, elem_iter->elem_key,
                elem_iter->elem_value);

        elem_iter = elem_iter->next;
    }

    int i;
    for (i = 0;i < to_merge->num_values;i++) {
        lily_hash_val *merging_hash = to_merge->elems[i]->value.hash;
        elem_iter = merging_hash->elem_chain;
        while (elem_iter) {
            lily_hash_set_elem(vm, result_hash, elem_iter->elem_key,
                    elem_iter->elem_value);

            elem_iter = elem_iter->next;
        }
    }

    lily_move_hash(result_reg, result_hash);
    lily_tag_value(vm, result_reg);
}

static void hash_select_reject_common(lily_vm_state *vm, uint16_t argc,
        uint16_t *code, int expect)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;
    lily_value *function_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];

    lily_hash_elem *elem_iter = hash_val->elem_chain;
    lily_vm_list *vm_list = vm->vm_list;
    int cached = 0;
    int vm_list_start = vm->vm_list->pos;

    lily_vm_list_ensure(vm, hash_val->num_elems * 2);

    hash_val->iter_count++;
    lily_jump_link *link = lily_jump_setup(vm->raiser);

    if (setjmp(link->jump) == 0) {
        while (elem_iter) {
            lily_value *e_key = elem_iter->elem_key;
            lily_value *e_value = elem_iter->elem_value;

            lily_value *result = lily_foreign_call(vm, &cached, 1,
                    function_reg, 2, e_key, e_value);

            if (result->value.integer == expect) {
                vm_list->values[vm_list->pos] = lily_copy_value(e_key);
                vm_list->values[vm_list->pos+1] = lily_copy_value(e_value);
                vm_list->pos += 2;
            }

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

void lily_hash_reject(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    hash_select_reject_common(vm, argc, code, 0);
}

void lily_hash_select(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    hash_select_reject_common(vm, argc, code, 1);
}

void lily_hash_size(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[1]]->value.hash;

    lily_move_integer(vm_regs[code[0]], hash_val->num_elems);
}

static const lily_func_seed clear =
    {NULL, "clear", dyna_function, "[A, B](Hash[A, B])", lily_hash_clear};

static const lily_func_seed delete_fn =
    {&clear, "delete", dyna_function, "[A, B](Hash[A, B], A)", lily_hash_delete};

static const lily_func_seed each_pair =
    {&delete_fn, "each_pair", dyna_function, "[A, B](Hash[A, B], Function(A, B))", lily_hash_each_pair};

static const lily_func_seed has_key =
    {&each_pair, "has_key", dyna_function, "[A, B](Hash[A, B], A):Boolean", lily_hash_has_key};

static const lily_func_seed keys =
    {&has_key, "keys", dyna_function, "[A, B](Hash[A, B]):List[A]", lily_hash_keys};

static const lily_func_seed get =
    {&keys, "get", dyna_function, "[A, B](Hash[A, B], A, B):B", lily_hash_get};

static const lily_func_seed map_values =
    {&get, "map_values", dyna_function, "[A, B, C](Hash[A, B], Function(B => C)):Hash[A, C]", lily_hash_map_values};

static const lily_func_seed merge =
    {&map_values, "merge", dyna_function, "[A, B](Hash[A, B], Hash[A, B]...):Hash[A, B]", lily_hash_merge};

static const lily_func_seed reject =
    {&merge, "reject", dyna_function, "[A, B](Hash[A, B], Function(A, B => Boolean)):Hash[A, B]", lily_hash_reject};

static const lily_func_seed select_fn =
    {&reject, "select", dyna_function, "[A, B](Hash[A, B], Function(A, B => Boolean)):Hash[A, B]", lily_hash_select};

static const lily_func_seed dynaload_start =
    {&select_fn, "size", dyna_function, "[A, B](Hash[A, B]):Integer", lily_hash_size};

static const lily_class_seed hash_seed =
{
    NULL,                 /* next */
    "Hash",               /* name */
    dyna_class,           /* load_type */
    1,                    /* is_refcounted */
    2,                    /* generic_count */
    0,                    /* flags */
    &dynaload_start,      /* dynaload_table */
    lily_destroy_hash,    /* destroy_func */
};

lily_class *lily_hash_init(lily_symtab *symtab)
{
    return lily_new_class_by_seed(symtab, &hash_seed);
}
