#include <stdio.h>

#include "lily_vm.h"

#include "lily_api_dynaload.h"
#include "lily_api_value.h"

void lily_double_to_i(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int64_t integer_val = (int64_t)vm_regs[code[1]]->value.doubleval;
    lily_value *result_reg = vm_regs[code[0]];

    lily_move_integer(result_reg, integer_val);
}

#define DYNA_NAME double

DYNA_FUNCTION(NULL, to_i, "(Double):Integer")

static lily_class_seed double_seed =
{
    NULL,               /* next */
    "Double",           /* name */
    dyna_class,         /* load_type */
    0,                  /* is_refcounted */
    0,                  /* generic_count */
    &seed_to_i          /* dynaload_table */
};

lily_class *lily_double_init(lily_symtab *symtab)
{
    return lily_new_class_by_seed(symtab, &double_seed);
}
