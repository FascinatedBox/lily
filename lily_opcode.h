#ifndef LILY_OPCODE_H
# define LILY_OPCODE_H

typedef enum {
    o_assign,
    /* Arguments: left, right
       Assigns the value of 'right' to 'left'. Assumes left and right both have
       the same type. */
    o_obj_assign,
    o_integer_add,
    /* lhs, rhs, result
       All integers */
    o_number_add,
    /* lhs, rhs, result
       lhs and rhs are either a number or an integer. result is a number. */
    o_integer_minus,
    o_number_minus,
    /* Handles builtin functions:
       var, func, #args, args... */
    o_func_call,
    o_vm_return
    /* Arguments: none
       Makes the vm function return. */
} lily_opcode;

#endif
