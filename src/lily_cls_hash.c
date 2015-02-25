#include "lily_impl.h"
#include "lily_vm.h"

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

static const lily_func_seed get =
    {"get", "function get[A, B](hash[A, B], A, B => B)", lily_hash_get, NULL};

#define SEED_START get

int lily_hash_setup(lily_class *cls)
{
    cls->seed_table = &SEED_START;
    return 1;
}
