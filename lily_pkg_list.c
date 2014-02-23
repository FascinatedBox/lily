#include "lily_impl.h"
#include "lily_pkg.h"
#include "lily_vm.h"

void lily_list_size(lily_vm_state *vm, uintptr_t *code, int num_args)
{
    lily_vm_register **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[0]]->value.list;
    lily_vm_register *ret_reg = vm_regs[code[1]];

    ret_reg->value.integer = list_val->num_values;
    ret_reg->flags &= ~SYM_IS_NIL;
}

static lily_func_seed size =
    {"size", lily_list_size,
        {SYM_CLASS_FUNCTION, 2, 0, SYM_CLASS_INTEGER, SYM_CLASS_LIST, SYM_CLASS_TEMPLATE, 0}};

lily_func_seed *list_seeds[] = {&size};
