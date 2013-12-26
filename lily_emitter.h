#ifndef LILY_EMITTER_H
# define LILY_EMITTER_H

# include "lily_raiser.h"
# include "lily_symtab.h"

typedef struct lily_block_ {
    /* This is where the position in code where the last loop started.
       while:  This is where the while was started.
       method: loop_start is set to -1 to indicate no loop.
       others: The loop_start of the previous block is used. This allows the
               loop_start of the last loop to 'bubble up' to the top if inside
               of one. */
    int loop_start;
    int patch_start;
    int block_type;
    int save_cache_start;
    lily_var *var_start;
    lily_var *method_var;
    struct lily_block_ *next;
    struct lily_block_ *prev;
} lily_block;

typedef struct {
    int *patches;
    int patch_pos;
    int patch_size;

    uintptr_t *save_cache;
    int save_cache_pos;
    int save_cache_size;

    lily_storage **storage_cache;
    int storage_cache_pos;
    int storage_cache_size;

    lily_var *top_var;
    lily_method_val *top_method;
    lily_sig *top_method_ret;

    int *lex_linenum;

    lily_block *first_block;
    lily_block *current_block;
    int method_depth;
    int block_depth;
    lily_raiser *raiser;
    lily_symtab *symtab;
    int expr_num;
} lily_emit_state;

# define BLOCK_IF     0x01
# define BLOCK_IFELSE 0x02
# define BLOCK_ANDOR  0x04
# define BLOCK_WHILE  0x10
# define BLOCK_FOR_IN 0x20
# define BLOCK_METHOD 0x40

void lily_emit_add_save_var(lily_emit_state *, lily_var *);
void lily_emit_ast(lily_emit_state *, lily_ast *);
void lily_emit_ast_to_var(lily_emit_state *, lily_ast *, lily_var *);
void lily_emit_break(lily_emit_state *);
void lily_emit_conditional(lily_emit_state *, lily_ast *);
void lily_emit_continue(lily_emit_state *);
void lily_emit_change_if_branch(lily_emit_state *, int);
void lily_emit_enter_block(lily_emit_state *, int);
void lily_emit_jump_if(lily_emit_state *, lily_ast *, int);
void lily_emit_leave_block(lily_emit_state *);
void lily_emit_return(lily_emit_state *, lily_ast *, lily_sig *);
void lily_emit_return_noval(lily_emit_state *);
void lily_emit_show(lily_emit_state *, lily_ast *);
void lily_emit_vm_return(lily_emit_state *);
void lily_free_emit_state(lily_emit_state *);
void lily_reset_main(lily_emit_state *);
void lily_emit_finalize_for_in(lily_emit_state *, lily_var *, lily_var *,
        lily_var *, lily_var *, int);
int lily_emit_try_enter_main(lily_emit_state *, lily_var *);
lily_emit_state *lily_new_emit_state(lily_raiser *);

#endif
