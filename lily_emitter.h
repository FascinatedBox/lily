#ifndef LILY_EMITTER_H
# define LILY_EMITTER_H

#include "lily_ast.h"

void lily_init_emitter(void);
void lily_emit_ast(lily_symbol *, lily_ast *);
void lily_emit_vm_return(lily_symbol *);

#endif
