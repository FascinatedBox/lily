#ifndef LILY_EMIT_TABLE_H
# define LILY_EMIT_TABLE_H

# include "lily_opcode.h"

/* The emitter table is used to get the opcode for 'generic' binary expressions.
   So far, this just means plus.
   This depends on a couple assumptions:
   * The expr_assign opcode is last.
   * Integer, double, and string have id 0, 1, and 2 respectively.
   Access it by [opcode][lhs id][rhs id]. -1 means the operation is not
   supported. */
static const int generic_binop_table[16][3][3] =
{
    {
        {o_integer_add, o_double_add, -1},
        {o_double_add, o_double_add, -1},
        {-1, -1, -1}
    },
    {
        {o_integer_minus, o_double_minus, -1},
        {o_double_minus, o_double_minus, -1},
        {-1, -1, -1}
    },
    {
        {o_is_equal, o_is_equal, -1},
        {o_is_equal, o_is_equal, -1},
        {-1, -1, o_is_equal}
    },
    {
        {o_less, o_less, -1},
        {o_less, o_less, -1},
        {-1, -1, o_less}
    },
    {
        {o_less_eq, o_less_eq, -1},
        {o_less_eq, o_less_eq, -1},
        {-1, -1, o_less_eq}
    },
    {
        {o_greater, o_greater, -1},
        {o_greater, o_greater, -1},
        {-1, -1, o_greater}
    },
    {
        {o_greater_eq, o_greater_eq, -1},
        {o_greater_eq, o_greater_eq, -1},
        {-1, -1, o_greater_eq}
    },
    {
        {o_not_eq, o_not_eq, -1},
        {o_not_eq, o_not_eq, -1},
        {-1, -1, o_not_eq}
    },
    {
        {o_modulo, -1, -1},
        {-1, -1, -1},
        {-1, -1, -1}
    },
    {
        {o_integer_mul, o_double_mul, -1},
        {o_double_mul, o_double_mul, -1},
        {-1, -1, -1}
    },
    {
        {o_integer_div, o_double_div, -1},
        {o_double_div, o_double_div, -1},
        {-1, -1, -1}
    },
    {
        {o_left_shift, -1, -1},
        {-1, -1, -1},
        {-1, -1, -1}
    },
    {
        {o_right_shift, -1, -1},
        {-1, -1, -1},
        {-1, -1, -1}
    },
    {
        {o_bitwise_and, -1, -1},
        {-1, -1, -1},
        {-1, -1, -1}
    },
    {
        {o_bitwise_or, -1, -1},
        {-1, -1, -1},
        {-1, -1, -1}
    },
    {
        {o_bitwise_xor, -1, -1},
        {-1, -1, -1},
        {-1, -1, -1}
    },
};
#endif
