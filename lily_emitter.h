#ifndef LILY_EMITTER_H
# define LILY_EMITTER_H

# include "lily_error.h"
# include "lily_symtab.h"

typedef struct {
    int *patches;
    int patch_pos;
    int patch_size;

    int *ctrl_patch_starts;
    int ctrl_patch_pos;
    int ctrl_patch_size;

    int *block_types;
    lily_var **block_var_starts;
    int block_pos;
    int block_size;

    uintptr_t *save_cache;
    int save_cache_pos;
    int save_cache_size;

    lily_method_val *target;
    lily_sig *target_ret;
    lily_method_val **method_vals;
    lily_sig **method_rets;
    lily_var **method_targets;
    int *method_id_offsets;
    int method_pos;
    int method_size;

    lily_excep_data *error;
    lily_symtab *symtab;
    int expr_num;
} lily_emit_state;

# define BLOCK_IF     0x1
# define BLOCK_IFELSE 0x2
# define BLOCK_METHOD 0x4

void lily_emit_ast(lily_emit_state *, lily_ast *);
void lily_emit_conditional(lily_emit_state *, lily_ast *);
void lily_emit_enter_method(lily_emit_state *, lily_var *);
void lily_emit_leave_method(lily_emit_state *);
void lily_emit_clear_block(lily_emit_state *, int);
void lily_emit_push_block(lily_emit_state *, int);
void lily_emit_pop_block(lily_emit_state *);
void lily_emit_return(lily_emit_state *, lily_ast *, lily_sig *);
void lily_emit_return_noval(lily_emit_state *);
void lily_emit_vm_return(lily_emit_state *);
void lily_free_emit_state(lily_emit_state *);
void lily_reset_main(lily_emit_state *);
lily_emit_state *lily_new_emit_state(lily_excep_data *);

#endif
