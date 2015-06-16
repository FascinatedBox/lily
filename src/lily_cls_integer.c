#include <inttypes.h>
#include <string.h>
#include <stdio.h>

#include "lily_alloc.h"
#include "lily_vm.h"
#include "lily_value.h"
#include "lily_seed.h"

int lily_integer_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    return (left->value.integer == right->value.integer);
}

/*  lily_integer_to_s
    Implements integer::to_s() */
void lily_integer_to_s(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int64_t integer_val = vm_regs[code[0]]->value.integer;
    lily_value *result_reg = vm_regs[code[1]];

    char buffer[32];
    snprintf(buffer, 32, "%"PRId64, integer_val);

    lily_string_val *new_sv = lily_malloc(sizeof(lily_string_val));
    char *text = lily_malloc(strlen(buffer) + 1);

    strcpy(text, buffer);
    new_sv->string = text;
    new_sv->size = strlen(buffer);
    new_sv->refcount = 1;

    lily_raw_value v = {.string = new_sv};
    lily_move_raw_value(vm, result_reg, 0, v);
}

void lily_integer_to_d(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int64_t integer_val = vm_regs[code[0]]->value.integer;
    lily_value *result_reg = vm_regs[code[1]];

    result_reg->flags = VAL_IS_PRIMITIVE;
    result_reg->value.doubleval = (double)integer_val;
}

static const lily_func_seed to_d =
    {NULL, "to_d", dyna_function, "function to_d(integer => double)", &lily_integer_to_d};

static const lily_func_seed dynaload_start =
    {&to_d, "to_s", dyna_function, "function to_s(integer => string)", &lily_integer_to_s};

static lily_class_seed integer_seed =
{
    NULL,               /* next */
    "integer",          /* name */
    dyna_class,         /* load_type */
    0,                  /* is_refcounted */
    0,                  /* generic_count */
    CLS_VALID_HASH_KEY, /* flags */
    &dynaload_start,    /* dynaload_table */
    NULL,               /* gc_marker */
    &lily_integer_eq,   /* eq_func */
    NULL,               /* destroy_func */
};

lily_class *lily_integer_init(lily_symtab *symtab)
{
    return lily_new_class_by_seed(symtab, &integer_seed);
}
