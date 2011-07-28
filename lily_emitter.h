#ifndef LILY_EMITTER_H
# define LILY_EMITTER_H

#include "lily_ast.h"

typedef enum {
    o_load_reg,
    o_builtin_print,
    o_vm_return
} lily_opcode;

void lily_init_emitter(void);
void lily_emit_ast(lily_symbol *, lily_ast *);
void lily_emit_vm_return(lily_symbol *);

#endif
