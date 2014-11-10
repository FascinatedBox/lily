#ifndef LILY_EMITTER_H
# define LILY_EMITTER_H

# include "lily_raiser.h"
# include "lily_symtab.h"

typedef struct lily_block_ {
    int loop_start;
    int patch_start;
    int block_type;

    lily_var *var_start;
    lily_var *function_var;
    int save_register_spot;
    lily_storage *storage_start;
    lily_class *class_entry;
    lily_storage *self;
    int generic_count;

    struct lily_block_ *next;
    struct lily_block_ *prev;
} lily_block;

typedef struct {
    int *patches;
    int patch_pos;
    int patch_size;

    lily_sig **sig_stack;
    int sig_stack_pos;
    int sig_stack_size;

    lily_var *top_var;
    lily_function_val *top_function;
    lily_sig *top_function_ret;

    lily_class *current_class;
    lily_storage *self_storage;

    uint16_t *lex_linenum;

    lily_storage *all_storage_start;
    lily_storage *unused_storage_start;
    lily_storage *all_storage_top;

    lily_block *first_block;
    lily_block *current_block;

    int function_depth;
    lily_raiser *raiser;
    lily_ast_str_pool *oo_name_pool;
    lily_symtab *symtab;
    int expr_num;
} lily_emit_state;

# define BLOCK_IF         0x0001
# define BLOCK_IF_ELIF    0x0002
# define BLOCK_IF_ELSE    0x0004
# define BLOCK_ANDOR      0x0010
# define BLOCK_WHILE      0x0020
# define BLOCK_DO_WHILE   0x0040
# define BLOCK_FOR_IN     0x0100
# define BLOCK_TRY        0x0200
# define BLOCK_TRY_EXCEPT 0x0400
# define BLOCK_CLASS      0x1000
# define BLOCK_FUNCTION   0x2000

void lily_emit_eval_condition(lily_emit_state *, lily_ast_pool *);
void lily_emit_eval_expr_to_var(lily_emit_state *, lily_ast_pool *,
        lily_var *);
void lily_emit_eval_expr(lily_emit_state *, lily_ast_pool *);
void lily_emit_finalize_for_in(lily_emit_state *, lily_var *, lily_var *,
        lily_var *, lily_var *, int);

void lily_emit_break(lily_emit_state *);
void lily_emit_continue(lily_emit_state *);
void lily_emit_return(lily_emit_state *, lily_ast *);

void lily_emit_change_block_to(lily_emit_state *emit, int);
void lily_emit_enter_block(lily_emit_state *, int);
void lily_emit_leave_block(lily_emit_state *);

void lily_emit_try(lily_emit_state *, int);
void lily_emit_except(lily_emit_state *, lily_class *, lily_var *, int);
void lily_emit_raise(lily_emit_state *, lily_ast *);

void lily_emit_update_function_block(lily_emit_state *, lily_class *, lily_sig *);

void lily_emit_vm_return(lily_emit_state *);
void lily_reset_main(lily_emit_state *);

void lily_free_emit_state(lily_emit_state *);
int lily_emit_try_enter_main(lily_emit_state *, lily_var *);
lily_emit_state *lily_new_emit_state(lily_raiser *);

#endif
