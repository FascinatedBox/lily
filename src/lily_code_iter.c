#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "lily_value_structs.h"

#include "lily_int_opcode.h"

#include "lily_api_code_iter.h"

void lily_ci_init(lily_code_iter *iter, uint16_t *buffer, uint16_t start,
        uint16_t stop)
{
    iter->buffer = buffer;
    iter->stop = stop;
    iter->offset = start;
    iter->round_total = 0;
}

void lily_ci_from_native(lily_code_iter *iter, lily_function_val *fv)
{
    iter->buffer = fv->code;
    iter->stop = fv->code_len;
    iter->offset = 0;
    iter->round_total = 0;
}

int lily_ci_next(lily_code_iter *iter)
{
    iter->offset += iter->round_total;

    if (iter->offset == iter->stop)
        return 0;

    memset(&iter->round_total, 0,
            sizeof(lily_code_iter) - offsetof(lily_code_iter, round_total));

    uint16_t *buffer = iter->buffer + iter->offset;

    iter->opcode = *buffer;

    /* The cast is so the compiler will warn about missing switch cases. */
    switch ((lily_opcode)*buffer) {
        case o_fast_assign:
        case o_assign:
            iter->line = 1;
            iter->inputs_3 = 1;
            iter->outputs_5 = 1;

            iter->round_total = 4;
            break;
        case o_integer_add:
        case o_integer_minus:
        case o_modulo:
        case o_integer_mul:
        case o_integer_div:
        case o_left_shift:
        case o_right_shift:
        case o_bitwise_and:
        case o_bitwise_or:
        case o_bitwise_xor:
        case o_double_add:
        case o_double_minus:
        case o_double_mul:
        case o_double_div:
        case o_is_equal:
        case o_not_eq:
        case o_less:
        case o_less_eq:
        case o_greater:
        case o_greater_eq:
            iter->line = 1;
            iter->inputs_3 = 2;
            iter->outputs_5 = 1;

            iter->round_total = 5;
            break;
        case o_jump:
            iter->jumps_7 = 1;

            iter->round_total = 2;
            break;
        case o_jump_if:
            iter->special_1 = 1;
            iter->inputs_3 = 1;
            iter->jumps_7 = 1;

            iter->round_total = 4;
            break;
        case o_native_call:
        case o_foreign_call:
        case o_function_call:
            iter->line = 1;
            iter->special_1 = 1;
            iter->counter_2 = 1;
            iter->outputs_5 = 1;
            iter->special_6 = buffer[3];

            iter->round_total = buffer[3] + 5;
            break;
        case o_return_val:
            iter->line = 1;
            iter->inputs_3 = 1;

            iter->round_total = 3;
            break;
        case o_return_unit:
            iter->line = 1;

            iter->round_total = 2;
            break;
        case o_unary_not:
        case o_unary_minus:
            iter->line = 1;
            iter->inputs_3 = 1;
            iter->outputs_5 = 1;

            iter->round_total = 4;
            break;
        case o_build_list:
        case o_build_tuple:
        case o_build_hash:
            iter->line = 1;
            iter->counter_2 = 1;
            iter->inputs_3 = buffer[2];
            iter->outputs_5 = 1;

            iter->round_total = buffer[2] + 4;
            break;
        case o_build_enum:
            iter->line = 1;
            iter->special_1 = 1;
            iter->counter_2 = 1;
            iter->inputs_3 = buffer[3];
            iter->outputs_5 = 1;

            iter->round_total = buffer[3] + 5;
            break;
        case o_dynamic_cast:
            iter->line = 1;
            iter->special_1 = 1;
            iter->inputs_3 = 1;
            iter->outputs_5 = 1;

            iter->round_total = 5;
            break;
        case o_integer_for:
            iter->line = 1;
            iter->inputs_3 = 3;
            iter->outputs_5 = 1;
            iter->jumps_7 = 1;

            iter->round_total = 7;
            break;
        case o_for_setup:
            iter->line = 1;
            iter->inputs_3 = 3;
            iter->outputs_5 = 1;

            iter->round_total = 6;
            break;
        case o_get_item:
            iter->line = 1;
            iter->inputs_3 = 2;
            iter->outputs_5 = 1;

            iter->round_total = 5;
            break;
        case o_set_item:
            iter->line = 1;
            iter->inputs_3 = 3;

            iter->round_total = 5;
            break;
        case o_set_global:
            iter->line = 1;
            iter->special_1 = 1;
            iter->inputs_3 = 1;

            iter->round_total = 4;
            break;
        case o_get_global:
        case o_get_readonly:
        case o_get_integer:
        case o_get_boolean:
            iter->line = 1;
            iter->special_1 = 1;
            iter->outputs_5 = 1;

            iter->round_total = 4;
            break;
        case o_get_property:
            iter->line = 1;
            iter->special_1 = 1;
            iter->inputs_3 = 1;
            iter->outputs_5 = 1;

            iter->round_total = 5;
            break;
        case o_set_property:
            iter->line = 1;
            iter->special_1 = 1;
            iter->inputs_3 = 2;

            iter->round_total = 5;
            break;
        case o_push_try:
            iter->line = 1;
            iter->jumps_7 = 1;

            iter->round_total = 3;
            break;
        case o_pop_try:
        case o_return_from_vm:

            iter->round_total = 1;
            break;
        case o_except_catch:
            iter->line = 1;
            iter->special_1 = 1;
            iter->outputs_5 = 1;
            iter->jumps_7 = 1;

            iter->round_total = 5;
            break;
        case o_except_ignore:
            iter->line = 1;
            iter->special_1 = 1;
            iter->special_6 = 1;
            iter->jumps_7 = 1;

            iter->round_total = 5;
            break;
        case o_raise:
            iter->line = 1;
            iter->inputs_3 = 1;

            iter->round_total = 3;
            break;
        case o_new_instance_basic:
        case o_new_instance_speculative:
        case o_new_instance_tagged:
            iter->line = 1;
            iter->special_1 = 1;
            iter->outputs_5 = 1;

            iter->round_total = 4;
            break;
        case o_optarg_dispatch:
            iter->special_1 = 1;
            iter->counter_2 = 1;
            iter->jumps_7 = buffer[2];

            iter->round_total = buffer[2] + 3;
            break;
        case o_match_dispatch:
            iter->line = 1;
            iter->special_1 = 2;
            iter->counter_2 = 1;
            iter->jumps_7 = buffer[4];

            iter->round_total = buffer[4] + 5;
            break;
        case o_variant_decompose:
            iter->line = 1;
            iter->special_1 = 1;
            iter->counter_2 = 1;
            iter->outputs_5 = buffer[3];

            iter->round_total = buffer[3] + 4;
            break;
        case o_get_upvalue:
            iter->line = 1;
            iter->special_1 = 1;
            iter->outputs_5 = 1;

            iter->round_total = 4;
            break;
        case o_set_upvalue:
            iter->line = 1;
            iter->special_1 = 1;
            iter->inputs_3 = 1;

            iter->round_total = 4;
            break;
        case o_create_closure:
            iter->line = 1;
            iter->special_1 = 1;
            iter->outputs_5 = 1;

            iter->round_total = 4;
            break;
        case o_create_function:
            iter->special_1 = 1;
            iter->special_4 = 1;
            iter->outputs_5 = 1;

            iter->round_total = 4;
            break;
        case o_load_class_closure:
            iter->line = 1;
            iter->special_1 = 1;
            iter->inputs_3 = 1;
            iter->outputs_5 = 1;

            iter->round_total = 5;
            break;
        case o_load_closure:
            iter->line = 1;
            iter->counter_2 = 1;
            iter->special_4 = buffer[2];
            iter->outputs_5 = 1;

            iter->round_total = buffer[2] + 4;
            break;
        case o_interpolation:
            iter->line = 1;
            iter->counter_2 = 1;
            iter->inputs_3 = buffer[2];
            iter->outputs_5 = 1;

            iter->round_total = buffer[2] + 4;
            break;
        default:
            return 0;
    }

    return 1;
}
