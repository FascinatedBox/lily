#include "lily_impl.h"
#include "lily_syminfo.h"
#include "lily_vm.h"

void lily_list_size(lily_vm_state *vm, uintptr_t *code, int num_args)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_list_val *list_val = vm_regs[code[0]]->value.list;
    lily_value *ret_reg = vm_regs[code[1]];

    ret_reg->value.integer = list_val->num_values;
    ret_reg->flags &= ~VAL_IS_NIL;
}

static const lily_func_seed size =
    {"size", lily_list_size, NULL,
        {SYM_CLASS_FUNCTION, 2, 0, SYM_CLASS_INTEGER, SYM_CLASS_LIST, SYM_CLASS_TEMPLATE, 0}};

#define SEED_START size

int lily_list_setup(lily_class *cls)
{
    cls->seed_table = &SEED_START;
    return 1;
}
