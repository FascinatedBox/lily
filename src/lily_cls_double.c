#include <stdio.h>

#include "lily_vm.h"
#include "lily_value.h"
#include "inttypes.h"
#include "lily_seed.h"

int lily_double_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    return (left->value.doubleval == right->value.doubleval);
}

void lily_double_to_i(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    double double_val = vm_regs[code[0]]->value.doubleval;
    lily_value *result_reg = vm_regs[code[1]];

    result_reg->flags = VAL_IS_PRIMITIVE;
    result_reg->value.integer = (int64_t)double_val;
}

static const lily_func_seed dynaload_start =
    {NULL, "to_i", dyna_function, "function to_i(double => integer)", lily_double_to_i};

static lily_class_seed double_seed =
{
    NULL,               /* next */
    "double",           /* name */
    dyna_class,         /* load_type */
    0,                  /* is_refcounted */
    0,                  /* generic_count */
    CLS_VALID_HASH_KEY, /* flags */
    &dynaload_start,    /* dynaload_table */
    NULL,               /* gc_marker */
    &lily_double_eq,    /* eq_func */
    NULL,               /* destroy_func */
};

lily_class *lily_double_init(lily_symtab *symtab)
{
    return lily_new_class_by_seed(symtab, &double_seed);
}
