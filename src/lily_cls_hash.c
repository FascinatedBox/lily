#include "lily_vm.h"
#include "lily_value.h"

#define malloc_mem(size) vm->mem_func(NULL, size)

void lily_hash_get(lily_vm_state *vm, lily_function_val *self, uint16_t *code)
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
void lily_hash_keys(lily_vm_state *vm, lily_function_val *self, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_hash_val *hash_val = vm_regs[code[0]]->value.hash;
    lily_value *result_reg = vm_regs[code[1]];

    int num_elems = hash_val->num_elems;

    lily_list_val *result_lv = malloc_mem(sizeof(lily_list_val));
    result_lv->num_values = num_elems;
    result_lv->visited = 0;
    result_lv->refcount = 1;
    result_lv->elems = malloc_mem(num_elems * sizeof(lily_value *));
    result_lv->gc_entry = NULL;

    lily_type *key_type = result_reg->type->subtypes[0];
    int i = 0;

    lily_hash_elem *elem_iter = hash_val->elem_chain;
    while (elem_iter) {
        lily_value *new_value = malloc_mem(sizeof(lily_value));
        new_value->type = key_type;
        new_value->value.integer = 0;
        new_value->flags = VAL_IS_NIL;

        lily_assign_value(vm, new_value, elem_iter->elem_key);
        result_lv->elems[i] = new_value;

        i++;
        elem_iter = elem_iter->next;
    }

    lily_deref(vm->mem_func, result_reg);

    result_reg->value.list = result_lv;
    result_reg->flags = 0;
}

static const lily_func_seed keys =
    {"keys", "function keys[A, B](hash[A, B] => list[A])", lily_hash_keys, NULL};

static const lily_func_seed get =
    {"get", "function get[A, B](hash[A, B], A, B => B)", lily_hash_get, &keys};

#define SEED_START get

int lily_hash_setup(lily_symtab *symtab, lily_class *cls)
{
    cls->seed_table = &SEED_START;
    return 1;
}
