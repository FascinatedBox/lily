#include "lily_impl.h"
#include "lily_vm.h"

void lily_hash_get(lily_vm_state *vm, lily_function_val *self, uintptr_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input = vm_regs[code[0]];
    lily_value *find_key = vm_regs[code[1]];
    lily_value *default_value = vm_regs[code[2]];
    lily_value *result = vm_regs[code[3]];

    if (input->flags & VAL_IS_NIL)
        lily_raise(vm->raiser, lily_SyntaxError, "Input is nil.\n");

    if (find_key->flags & VAL_IS_NIL) {
        /* Hashes don't have nil keys, so this key won't be found. */
        lily_assign_value(vm, result, default_value);
        return;
    }

    uint64_t siphash = lily_calculate_siphash(vm->sipkey, find_key);
    lily_hash_elem *hash_elem = lily_try_lookup_hash_elem(input->value.hash,
            siphash, find_key);

    lily_value *new_value;
    if (hash_elem)
        new_value = hash_elem->elem_value;
    else
        new_value = default_value;

    lily_assign_value(vm, result, new_value);
}

/* Hashes have two template paramaters:
   * The key (K) is 0.
   * The value (V) is 1. */
static const lily_func_seed get =
    {"get", lily_hash_get, NULL,
        {SYM_CLASS_FUNCTION, 4, 0,
            /* Returns 'V' */
            SYM_CLASS_TEMPLATE, 1,
            /* Input: Hash[K, V] (any hash). */
            SYM_CLASS_HASH, SYM_CLASS_TEMPLATE, 0, SYM_CLASS_TEMPLATE, 1,
            /* Locate: K */
            SYM_CLASS_TEMPLATE, 0,
            /* Default: V*/
            SYM_CLASS_TEMPLATE, 1
        }
    };

#define SEED_START get

int lily_hash_setup(lily_class *cls)
{
    cls->seed_table = &SEED_START;
    return 1;
}
