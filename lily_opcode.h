#ifndef LILY_OPCODE_H
# define LILY_OPCODE_H

typedef enum {
    o_load_reg,
    o_builtin_print,
    o_assign,
    o_vm_return
} lily_opcode;

#endif
