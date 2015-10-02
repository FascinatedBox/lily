#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "lily_alloc.h"
#include "lily_vm.h"
#include "lily_value.h"
#include "lily_seed.h"

#include "lily_cls_integer.h"

/*  Implements boolean::to_i
    This converts a boolean value to an integer. Boolean values are represented
    as integers internally, so this only needs to shift the value over. */
void lily_boolean_to_i(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input_reg = vm_regs[code[1]];
    lily_value *result_reg = vm_regs[code[0]];

    result_reg->value.integer = input_reg->value.integer;
    result_reg->flags = VAL_IS_PRIMITIVE;
}

/*  Implements boolean::to_s
    This converts a boolean value to a string. The result will be either "true"
    or "false". */
void lily_boolean_to_s(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int64_t input = vm_regs[code[1]]->value.integer;
    lily_value *result_reg = vm_regs[code[0]];
    char *to_copy;
    if (input == 0)
        to_copy = "false";
    else
        to_copy = "true";

    lily_string_val *sv = lily_malloc(sizeof(lily_string_val));
    sv->string = lily_malloc(strlen(to_copy) + 1);
    strcpy(sv->string, to_copy);
    sv->refcount = 1;
    sv->size = strlen(to_copy);

    lily_raw_value v = {.string = sv};

    lily_move_raw_value(result_reg, v);
}

static const lily_func_seed to_i =
    {NULL, "to_i", dyna_function, "(boolean => integer)", &lily_boolean_to_i};

static const lily_func_seed dynaload_start =
    {&to_i, "to_s", dyna_function, "(boolean => string)", &lily_boolean_to_s};

static lily_class_seed boolean_seed =
{
    NULL,               /* next */
    "boolean",          /* name */
    dyna_class,         /* load_type */
    0,                  /* is_refcounted */
    0,                  /* generic_count */
    0,                  /* flags */
    &dynaload_start,    /* dynaload_table */
    NULL,               /* gc_marker */
    &lily_integer_eq,   /* eq_func */
    NULL,               /* destroy_func */
};

lily_class *lily_boolean_init(lily_symtab *symtab)
{
    return lily_new_class_by_seed(symtab, &boolean_seed);
}
