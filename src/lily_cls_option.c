#include "lily_alloc.h"
#include "lily_core_types.h"
#include "lily_vm.h"
#include "lily_value.h"
#include "lily_seed.h"
#include "lily_cls_option.h"

/* Option is a dynaloaded enum. There are no implementations for a marker, eq,
   or destroy because enums use tuple's versions of those. This instead focuses
   on the methods available to Option. */

/* Symtab assigns a 'variant id' to each variant that it sees, in order. This is
   later used by vm to determine what thing occupies the enum (ex: match). */

#define SOME_VARIANT_ID 0
#define NONE_VARIANT_ID 1

inline lily_instance_val *lily_new_option_some(lily_value *v)
{
    return lily_new_enum_1(SYM_CLASS_OPTION, SOME_VARIANT_ID, v);
}

/* Since None has no arguments, it has a backing literal to represent it. This
   dives into the vm's class table to get the backing literal of the None. */
lily_instance_val *lily_get_option_none(lily_vm_state *vm)
{
    lily_class *opt_class = vm->class_table[SYM_CLASS_OPTION];
    lily_variant_class *none_cls = opt_class->variant_members[NONE_VARIANT_ID];
    return none_cls->default_value->value.instance;
}

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

        source = lily_new_option_some(lily_copy_value(output));
        lily_move_enum(result_reg, source);
    }
    else {
        source = lily_get_option_none(vm);
        lily_move_shared_enum(result_reg, source);
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

static const lily_func_seed and_fn =
    {NULL, "and", dyna_function, "[A, B](Option[A], Option[B]):Option[B]", &lily_option_and};

static const lily_func_seed and_then =
    {&and_fn, "and_then", dyna_function, "[A, B](Option[A], Function(A => Option[B])):Option[B]", &lily_option_and_then};

static const lily_func_seed is_none =
    {&and_then, "is_none", dyna_function, "[A](Option[A]):Boolean", &lily_option_is_none};

static const lily_func_seed is_some =
    {&is_none, "is_some", dyna_function, "[A](Option[A]):Boolean", &lily_option_is_some};

static const lily_func_seed map =
    {&is_some, "map", dyna_function, "[A, B](Option[A], Function(A => B)):Option[B]", &lily_option_map};

static const lily_func_seed or_fn =
    {&map, "or", dyna_function, "[A](Option[A], Option[A]):Option[A]", &lily_option_or};

static const lily_func_seed or_else =
    {&or_fn, "or_else", dyna_function, "[A](Option[A], Function( => Option[A])):Option[A]", &lily_option_or_else};

static const lily_func_seed unwrap =
    {&or_else, "unwrap", dyna_function, "[A](Option[A]):A", &lily_option_unwrap};

static const lily_func_seed unwrap_or =
    {&unwrap, "unwrap_or", dyna_function, "[A](Option[A], A):A", &lily_option_unwrap_or};

const lily_func_seed lily_option_dl_start =
    {&unwrap_or, "unwrap_or_else", dyna_function, "[A](Option[A], Function( => A)):A", &lily_option_unwrap_or_else};
