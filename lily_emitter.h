#ifndef LILY_EMITTER_H
# define LILY_EMITTER_H

# include "lily_ast.h"
# include "lily_symtab.h"
# include "lily_interp.h"

typedef struct {
    int next_reg;
    lily_code_data *target;
    lily_interp *interp;
} lily_emit_state;

lily_emit_state *lily_init_emit_state(lily_interp *);
void lily_free_emit_state(lily_emit_state *);
void lily_emit_ast(lily_emit_state *, lily_ast *);
void lily_emit_vm_return(lily_emit_state *);
void lily_emit_set_target(lily_emit_state *, lily_symbol *);

#endif
