#ifndef LILY_EMITTER_H
# define LILY_EMITTER_H

# include "lily_raiser.h"
# include "lily_symtab.h"

typedef struct {
    int *patches;
    int patch_pos;
    int patch_size;

    int *ctrl_patch_starts;
    int ctrl_patch_pos;
    int ctrl_patch_size;

    int *block_types;
    int *block_save_starts;
    lily_var **block_var_starts;
    int block_pos;
    int block_size;

    uintptr_t *save_cache;
    int save_cache_pos;
    int save_cache_size;

    lily_storage **storage_cache;
    int storage_cache_pos;
    int storage_cache_size;

    lily_var *top_var;
    lily_method_val *top_method;
    lily_sig *top_method_ret;
    lily_var **method_vars;
    int *method_id_offsets;
    int method_pos;
    int method_size;

    lily_raiser *raiser;
    lily_symtab *symtab;
    int expr_num;
} lily_emit_state;

# define BLOCK_IF     0x01
# define BLOCK_IFELSE 0x02
# define BLOCK_ANDOR  0x04
# define BLOCK_METHOD 0x10

void lily_emit_add_save_var(lily_emit_state *, lily_var *);
void lily_emit_ast(lily_emit_state *, lily_ast *);
void lily_emit_conditional(lily_emit_state *, lily_ast *);
void lily_emit_enter_method(lily_emit_state *, lily_var *);
void lily_emit_leave_method(lily_emit_state *);
void lily_emit_change_if_branch(lily_emit_state *, int);
void lily_emit_enter_block(lily_emit_state *, int);
void lily_emit_leave_block(lily_emit_state *);
void lily_emit_return(lily_emit_state *, lily_ast *, lily_sig *);
void lily_emit_return_noval(lily_emit_state *);
void lily_emit_update_return(lily_emit_state *);
void lily_emit_vm_return(lily_emit_state *);
void lily_free_emit_state(lily_emit_state *);
void lily_reset_main(lily_emit_state *);
lily_emit_state *lily_new_emit_state(lily_raiser *);

#endif
