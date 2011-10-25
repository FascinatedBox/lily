#ifndef LILY_OPCODE_H
# define LILY_OPCODE_H

typedef enum {
    o_builtin_print,
    /* Arguments: str s
       Calls lily_impl_print, using the str 's'. */
    o_assign,
    /* Arguments: left, right
       Assigns the value of 'right' to 'left'. Assumes left and right both have
       the same type. */
    o_vm_return
    /* Arguments: none
       Makes the vm function return. */
} lily_opcode;

#endif
