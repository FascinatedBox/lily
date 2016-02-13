#include <stdio.h>

#include "lily_vm.h"
#include "lily_value.h"
#include "lily_seed.h"

void lily_double_to_i(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int64_t integer_val = (int64_t)vm_regs[code[1]]->value.doubleval;
    lily_value *result_reg = vm_regs[code[0]];

    lily_move_integer(result_reg, integer_val);
}

static const lily_func_seed dynaload_start =
    {NULL, "to_i", dyna_function, "(Double):Integer", lily_double_to_i};

static lily_class_seed double_seed =
{
    NULL,               /* next */
    "Double",           /* name */
    dyna_class,         /* load_type */
    0,                  /* is_refcounted */
    0,                  /* generic_count */
    &dynaload_start,    /* dynaload_table */
    NULL,               /* destroy_func */
};

lily_class *lily_double_init(lily_symtab *symtab)
{
    return lily_new_class_by_seed(symtab, &double_seed);
}
