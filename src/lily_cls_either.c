#include "lily_core_types.h"
#include "lily_vm.h"
#include "lily_value.h"
#include "lily_seed.h"
#include "lily_cls_option.h"

#define RIGHT_VARIANT_ID 0
#define LEFT_VARIANT_ID  1

lily_instance_val *lily_new_left(lily_value *v)
{
    return lily_new_enum_1(SYM_CLASS_EITHER, LEFT_VARIANT_ID, v);
}

lily_instance_val *lily_new_right(lily_value *v)
{
    return lily_new_enum_1(SYM_CLASS_EITHER, RIGHT_VARIANT_ID, v);
}

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

const lily_func_seed is_left =
    {NULL, "is_left", dyna_function, "[A,B](Either[A,B]):Boolean", &lily_either_is_left};

const lily_func_seed is_right =
    {&is_left, "is_right", dyna_function, "[A,B](Either[A,B]):Boolean", &lily_either_is_right};

const lily_func_seed get_left =
    {&is_right, "left", dyna_function, "[A,B](Either[A,B]):Option[A]", &lily_either_left};

const lily_func_seed lily_either_dl_start =
    {&get_left, "right", dyna_function, "[A,B](Either[A,B]):Option[B]", &lily_either_right};
