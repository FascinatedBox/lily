#ifndef LILY_EMITTER_H
# define LILY_EMITTER_H

# include "lily_error.h"
# include "lily_symtab.h"

typedef struct {
    int *patches;
    int *saved_spots;
    int patch_pos;
    int patch_size;
    int save_pos;
    int save_size;
} lily_branches;

typedef struct {
    lily_branches *branches;
    lily_method_val *target;
    lily_excep_data *error;
    lily_symtab *symtab;
    int expr_num;
} lily_emit_state;

void lily_emit_ast(lily_emit_state *, lily_ast *);
void lily_emit_conditional(lily_emit_state *, lily_ast *);
void lily_emit_new_if(lily_emit_state *);
void lily_emit_branch_change(lily_emit_state *);
void lily_emit_fix_exit_jumps(lily_emit_state *);
void lily_emit_set_target(lily_emit_state *, lily_var *);
void lily_emit_vm_return(lily_emit_state *);
void lily_free_emit_state(lily_emit_state *);
lily_emit_state *lily_new_emit_state(lily_excep_data *);

#endif
