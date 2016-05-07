#include "lily_core_types.h"
#include "lily_vm.h"

#include "lily_api_dynaload.h"
#include "lily_api_value.h"

static void either_is_left_right(lily_vm_state *vm, uint16_t *code, int expect)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_instance_val *iv = vm_regs[code[1]]->value.instance;
    lily_value *result_reg = vm_regs[code[0]];

    lily_move_boolean(result_reg, (iv->variant_id == expect));
}

void lily_either_is_left(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    either_is_left_right(vm, code, LEFT_VARIANT_ID);
}

void lily_either_is_right(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    either_is_left_right(vm, code, RIGHT_VARIANT_ID);
}

static void either_optionize_left_right(lily_vm_state *vm, uint16_t *code, int expect)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_instance_val *iv = vm_regs[code[1]]->value.instance;
    lily_value *result_reg = vm_regs[code[0]];

    if (iv->variant_id == expect)
        lily_move_enum_f(MOVE_DEREF_SPECULATIVE, result_reg,
                lily_new_some(lily_copy_value(iv->values[0])));
    else
        lily_move_enum_f(MOVE_DEREF_NO_GC, result_reg, lily_get_none(vm));
}

void lily_either_left(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    either_optionize_left_right(vm, code, LEFT_VARIANT_ID);
}

void lily_either_right(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    either_optionize_left_right(vm, code, RIGHT_VARIANT_ID);
}

#define DYNA_NAME either

DYNA_FUNCTION(NULL,           is_left,  "[A,B](Either[A,B]):Boolean")
DYNA_FUNCTION(&seed_is_left,  is_right, "[A,B](Either[A,B]):Boolean")
DYNA_FUNCTION(&seed_is_right, left,     "[A,B](Either[A,B]):Option[A]")
DYNA_FUNCTION_RAW(, either, &seed_left, lily_either_dl_start, right, "[A,B](Either[A,B]):Option[B]")
