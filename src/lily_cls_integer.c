#include <inttypes.h>
#include <string.h>
#include <stdio.h>

#include "lily_alloc.h"
#include "lily_vm.h"
#include "lily_value.h"

int lily_integer_eq(lily_vm_state *vm, int *depth, lily_value *left,
        lily_value *right)
{
    return (left->value.integer == right->value.integer);
}

/*  lily_integer_to_s
    Implements integer::to_s() */
void lily_integer_to_s(lily_vm_state *vm, lily_function_val *self,
        uint16_t *code)
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

void lily_integer_to_d(lily_vm_state *vm, lily_function_val *self,
        uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int64_t integer_val = vm_regs[code[0]]->value.integer;
    lily_value *result_reg = vm_regs[code[1]];

    result_reg->flags = 0;
    result_reg->value.doubleval = (double)integer_val;
}

static const lily_func_seed to_d =
    {"to_d", "function to_d(integer => double)", lily_integer_to_d, NULL};

static const lily_func_seed to_s =
    {"to_s", "function to_s(integer => string)", lily_integer_to_s, &to_d};

int lily_integer_setup(lily_symtab *symtab, lily_class *cls)
{
    cls->seed_table = &to_s;
    return 1;
}

