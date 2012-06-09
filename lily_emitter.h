#ifndef LILY_EMITTER_H
# define LILY_EMITTER_H

# include "lily_error.h"
# include "lily_symtab.h"

typedef struct {
    int *patches;
    int *saved_spots;
    int *types;
    lily_var **saved_vars;
    int patch_pos;
    int patch_size;
    int block_pos;
    int block_size;
} lily_branches;

typedef struct {
    lily_branches *branches;
    lily_method_val *target;
    lily_sig *target_ret;
    lily_var *target_var;
    lily_var *saved_var;
    lily_excep_data *error;
    lily_symtab *symtab;
    int expr_num;
} lily_emit_state;

# define BLOCK_IF     0x1
# define BLOCK_IFELSE 0x2
# define BLOCK_RETURN 0x4

void lily_emit_ast(lily_emit_state *, lily_ast *);
void lily_emit_conditional(lily_emit_state *, lily_ast *);
void lily_emit_enter_method(lily_emit_state *, lily_var *);
void lily_emit_leave_method(lily_emit_state *);
void lily_emit_clear_block(lily_emit_state *, int);
void lily_emit_push_block(lily_emit_state *, int);
void lily_emit_pop_block(lily_emit_state *);
void lily_emit_set_target(lily_emit_state *, lily_var *);
void lily_emit_return(lily_emit_state *, lily_ast *, lily_sig *);
void lily_emit_vm_return(lily_emit_state *);
void lily_free_emit_state(lily_emit_state *);
lily_emit_state *lily_new_emit_state(lily_excep_data *);

#endif
