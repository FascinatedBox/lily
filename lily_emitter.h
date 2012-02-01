#ifndef LILY_EMITTER_H
# define LILY_EMITTER_H

# include "lily_error.h"
# include "lily_symtab.h"

typedef struct {
    lily_method_val *target;
    lily_excep_data *error;
    lily_symtab *symtab;
    int expr_num;
} lily_emit_state;

void lily_emit_ast(lily_emit_state *, lily_ast *);
void lily_emit_set_target(lily_emit_state *, lily_var *);
void lily_emit_vm_return(lily_emit_state *);
void lily_free_emit_state(lily_emit_state *);
lily_emit_state *lily_new_emit_state(lily_excep_data *);

#endif
