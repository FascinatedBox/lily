#include "lily_vm.h"

#include "lily_api_alloc.h"
#include "lily_api_dynaload.h"
#include "lily_api_value_ops.h"

void lily_dynamic_new(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result = vm_regs[code[0]];
    lily_value *input = vm_regs[code[1]];

    if (input->flags & VAL_IS_DEREFABLE)
        input->value.generic->refcount++;

    lily_dynamic_val *dynamic_val = lily_new_dynamic_val();

    *(dynamic_val->inner_value) = *input;
    lily_move_dynamic(result, dynamic_val);
    lily_tag_value(vm, result);
}

DYNA_FUNCTION_RAW(, dynamic, NULL, lily_dynamic_dl_start, new, "[A](A):Dynamic")
