#ifndef LILY_OPCODE_H
# define LILY_OPCODE_H

typedef enum {
    o_assign,
    /* Arguments: left, right
       Assigns the value of 'right' to 'left'. Assumes left and right both have
       the same type. */
    o_obj_assign,
    o_str_assign,
    o_integer_add,
    /* lhs, rhs, result
       All integers */
    o_number_add,
    /* lhs, rhs, result
       lhs and rhs are either a number or an integer. result is a number. */
    o_integer_minus,
    o_number_minus,
    /* lhs, rhs, result
       lhs and rhs could be both integer, integer and number, or both str. */
    o_is_equal,
    o_less,
    o_less_eq,
    o_greater,
    o_greater_eq,
    o_not_eq,

    /* pos
       Jump to pos. */
    o_jump,
    /* value, pos
       Jump to pos if value is nil or 0. */
    o_jump_if_false,
    /* Handles builtin functions:
       var, func, #args, args... */
    o_func_call,
    o_method_call,
    o_return_val,
    o_return_noval,
    o_save,
    o_restore,
    o_unary_not,
    o_unary_minus,
    o_vm_return
    /* Arguments: none
       Makes the vm function return. */
} lily_opcode;

#endif
