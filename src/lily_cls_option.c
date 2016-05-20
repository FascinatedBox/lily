#include "lily_core_types.h"
#include "lily_vm.h"

#include "lily_api_alloc.h"
#include "lily_api_dynaload.h"
#include "lily_api_value_ops.h"

void lily_option_and(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *opt_reg = vm_regs[code[1]];
    lily_value *and_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];
    lily_value *source;

    if (opt_reg->value.instance->variant_id == SOME_VARIANT_ID)
        source = and_reg;
    else
        source = opt_reg;

    lily_assign_value(result_reg, source);
}

void lily_option_and_then(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *opt_reg = vm_regs[code[1]];
    lily_value *function_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];
    lily_instance_val *optval = opt_reg->value.instance;
    lily_value *source;
    int cached = 0;

    if (optval->variant_id == SOME_VARIANT_ID) {
        lily_value *output = lily_foreign_call(vm, &cached, 1,
                function_reg, 1, optval->values[0]);

        source = output;
    }
    else
        source = opt_reg;

    lily_assign_value(result_reg, source);
}

static void option_is_some_or_none(lily_vm_state *vm, uint16_t argc,
        uint16_t *code, int num_expected)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_instance_val *optval = vm_regs[code[1]]->value.instance;
    lily_value *result_reg = vm_regs[code[0]];

    lily_move_boolean(result_reg, (optval->num_values == num_expected));
}

void lily_option_map(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *opt_reg = vm_regs[code[1]];
    lily_value *function_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];
    lily_instance_val *optval = opt_reg->value.instance;
    lily_instance_val *source;
    int cached = 0;

    if (optval->variant_id == SOME_VARIANT_ID) {
        lily_value *output = lily_foreign_call(vm, &cached, 1,
                function_reg, 1, optval->values[0]);

        source = lily_new_some(lily_copy_value(output));
        lily_move_enum_f(MOVE_DEREF_SPECULATIVE, result_reg, source);
    }
    else {
        source = lily_get_none(vm);
        lily_move_enum_f(MOVE_SHARED_SPECULATIVE, result_reg, source);
    }
}

void lily_option_is_some(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    option_is_some_or_none(vm, argc, code, 1);
}

void lily_option_is_none(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    option_is_some_or_none(vm, argc, code, 0);
}

void lily_option_or(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *opt_reg = vm_regs[code[1]];
    lily_value *or_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];
    lily_value *source;

    if (opt_reg->value.instance->variant_id == SOME_VARIANT_ID)
        source = opt_reg;
    else
        source = or_reg;

    lily_assign_value(result_reg, source);
}

void lily_option_unwrap(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *opt_reg = vm_regs[code[1]];
    lily_instance_val *optval = opt_reg->value.instance;
    lily_value *result_reg = vm_regs[code[0]];

    if (optval->variant_id == SOME_VARIANT_ID)
        lily_assign_value(result_reg, opt_reg->value.instance->values[0]);
    else
        lily_raise(vm->raiser, lily_ValueError, "unwrap called on None.\n");
}

void lily_option_unwrap_or(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *opt_reg = vm_regs[code[1]];
    lily_value *fallback_reg = vm_regs[code[2]];
    lily_instance_val *optval = opt_reg->value.instance;
    lily_value *result_reg = vm_regs[code[0]];
    lily_value *source;

    if (optval->variant_id == SOME_VARIANT_ID)
        source = opt_reg->value.instance->values[0];
    else
        source = fallback_reg;

    lily_assign_value(result_reg, source);
}

void lily_option_or_else(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *opt_reg = vm_regs[code[1]];
    lily_value *function_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];
    lily_instance_val *optval = opt_reg->value.instance;
    lily_value *source;
    int cached = 0;

    if (optval->variant_id == SOME_VARIANT_ID)
        source = opt_reg;
    else
        source = lily_foreign_call(vm, &cached, 1, function_reg, 0);

    lily_assign_value(result_reg, source);
}

void lily_option_unwrap_or_else(lily_vm_state *vm, uint16_t argc, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *opt_reg = vm_regs[code[1]];
    lily_value *function_reg = vm_regs[code[2]];
    lily_value *result_reg = vm_regs[code[0]];
    lily_instance_val *optval = opt_reg->value.instance;
    lily_value *source;
    int cached = 0;

    if (optval->variant_id == SOME_VARIANT_ID)
        source = opt_reg->value.instance->values[0];
    else
        source = lily_foreign_call(vm, &cached, 1, function_reg, 0);

    lily_assign_value(result_reg, source);
}

#define DYNA_NAME option

DYNA_FUNCTION(NULL,            and,            "[A, B](Option[A], Option[B]):Option[B]")
DYNA_FUNCTION(&seed_and,       and_then,       "[A, B](Option[A], Function(A => Option[B])):Option[B]")
DYNA_FUNCTION(&seed_and_then,  is_none,        "[A](Option[A]):Boolean")
DYNA_FUNCTION(&seed_is_none,   is_some,        "[A](Option[A]):Boolean")
DYNA_FUNCTION(&seed_is_some,   map,            "[A, B](Option[A], Function(A => B)):Option[B]")
DYNA_FUNCTION(&seed_map,       or,             "[A](Option[A], Option[A]):Option[A]")
DYNA_FUNCTION(&seed_or,        or_else,        "[A](Option[A], Function( => Option[A])):Option[A]")
DYNA_FUNCTION(&seed_or_else,   unwrap,         "[A](Option[A]):A")
DYNA_FUNCTION(&seed_unwrap,    unwrap_or,      "[A](Option[A], A):A")
DYNA_FUNCTION(&seed_unwrap_or, unwrap_or_else, "[A](Option[A], Function( => A)):A")
DYNA_FUNCTION_RAW(, option, &seed_unwrap_or, lily_option_dl_start, unwrap_or_else, "[A](Option[A], Function( => A)):A")
