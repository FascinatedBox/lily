#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "lily_int_code_iter.h"
#include "lily_int_opcode.h"

#include "lily_value_structs.h"

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

    if (iter->offset >= iter->stop)
        return 0;

    memset(&iter->round_total, 0,
            sizeof(lily_code_iter) - offsetof(lily_code_iter, round_total));

    uint16_t *buffer = iter->buffer + iter->offset;

    iter->opcode = *buffer;

    /* The cast is so the compiler will warn about missing switch cases. */
    switch ((lily_opcode)*buffer) {
        case o_assign:
        case o_assign_noref:
        case o_unary_not:
        case o_unary_minus:
        case o_unary_bitwise_not:
            iter->inputs_3 = 1;
            iter->outputs_4 = 1;
            iter->line_6 = 1;

            iter->round_total = 4;
            break;
        case o_int_add:
        case o_int_minus:
        case o_int_modulo:
        case o_int_multiply:
        case o_int_divide:
        case o_int_left_shift:
        case o_int_right_shift:
        case o_int_bitwise_and:
        case o_int_bitwise_or:
        case o_int_bitwise_xor:
        case o_number_add:
        case o_number_minus:
        case o_number_multiply:
        case o_number_divide:
        case o_compare_eq:
        case o_compare_not_eq:
        case o_compare_greater:
        case o_compare_greater_eq:
        case o_subscript_get:
            iter->inputs_3 = 2;
            iter->outputs_4 = 1;
            iter->line_6 = 1;

            iter->round_total = 5;
            break;
        case o_jump:
            iter->jumps_5 = 1;

            iter->round_total = 2;
            break;
        case o_jump_if:
        case o_jump_if_not_class:
            iter->special_1 = 1;
            iter->inputs_3 = 1;
            iter->jumps_5 = 1;

            iter->round_total = 4;
            break;
        case o_jump_if_set:
            iter->inputs_3 = 1;
            iter->jumps_5 = 1;

            iter->round_total = 3;
            break;
        case o_call_native:
        case o_call_foreign:
        case o_call_register:
            iter->special_1 = 1;
            iter->counter_2 = 1;
            iter->inputs_3 = buffer[2];
            iter->outputs_4 = 1;
            iter->line_6 = 1;

            iter->round_total = buffer[2] + 5;
            break;
        case o_return_value:
        case o_exception_raise:
            iter->inputs_3 = 1;
            iter->line_6 = 1;

            iter->round_total = 3;
            break;
        case o_return_unit:
            iter->line_6 = 1;

            iter->round_total = 2;
            break;
        case o_build_list:
        case o_build_tuple:
        case o_interpolation:
            iter->counter_2 = 1;
            iter->inputs_3 = buffer[1];
            iter->outputs_4 = 1;
            iter->line_6 = 1;

            iter->round_total = buffer[1] + 4;
            break;
        case o_build_hash:
        case o_build_variant:
            iter->special_1 = 1;
            iter->counter_2 = 1;
            iter->inputs_3 = buffer[2];
            iter->outputs_4 = 1;
            iter->line_6 = 1;

            iter->round_total = buffer[2] + 5;
            break;
        case o_for_integer:
            iter->inputs_3 = 3;
            iter->outputs_4 = 1;
            iter->jumps_5 = 1;
            iter->line_6 = 1;

            iter->round_total = 7;
            break;
        case o_for_setup:
            iter->inputs_3 = 3;
            iter->outputs_4 = 1;
            iter->line_6 = 1;

            iter->round_total = 6;
            break;
        case o_subscript_set:
            iter->inputs_3 = 3;
            iter->line_6 = 1;

            iter->round_total = 5;
            break;
        case o_global_set:
        case o_closure_set:
            iter->special_1 = 1;
            iter->inputs_3 = 1;
            iter->line_6 = 1;

            iter->round_total = 4;
            break;
        case o_global_get:
        case o_load_readonly:
        case o_load_integer:
        case o_load_boolean:
        case o_load_byte:
        case o_load_empty_variant:
        case o_closure_get:
        case o_instance_new:
        case o_closure_new:
        case o_closure_function:
            iter->special_1 = 1;
            iter->outputs_4 = 1;
            iter->line_6 = 1;

            iter->round_total = 4;
            break;
        case o_property_get:
            iter->special_1 = 1;
            iter->inputs_3 = 1;
            iter->outputs_4 = 1;
            iter->line_6 = 1;

            iter->round_total = 5;
            break;
        case o_property_set:
            iter->special_1 = 1;
            iter->inputs_3 = 2;
            iter->line_6 = 1;

            iter->round_total = 5;
            break;
        case o_catch_push:
            iter->jumps_5 = 1;
            iter->line_6 = 1;

            iter->round_total = 3;
            break;
        case o_catch_pop:
        case o_vm_exit:

            iter->round_total = 1;
            break;
        case o_exception_catch:
            iter->special_1 = 1;
            iter->jumps_5 = 1;
            iter->line_6 = 1;

            iter->round_total = 4;
            break;
        case o_exception_store:
            iter->outputs_4 = 1;

            iter->round_total = 2;
            break;
    }

    return 1;
}
