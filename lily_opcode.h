#ifndef LILY_OPCODE_H
# define LILY_OPCODE_H

typedef enum {
    o_load_reg,
    /* Arguments: pos, v
       Loads the symbol 'v' into the vm register at 'pos'. */
    o_builtin_print,
    /* Arguments: pos
       Calls lily_impl_print, using the symbol at vm register 'pos'. */
    o_assign,
    /* Arguments: left_pos, right_pos
       Assigns the value in 'right_pos' to 'left_pos'. 'left_pos' and
       'right_pos' both refer to vm registers. */
    o_vm_return
    /* Arguments: none
       This returns control from the lily vm to the parser. */
} lily_opcode;

#endif
