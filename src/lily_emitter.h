#ifndef LILY_EMITTER_H
# define LILY_EMITTER_H

# include "lily_raiser.h"
# include "lily_symtab.h"

typedef struct lily_block_ {
    int16_t loop_start;
    uint16_t patch_start;
    uint16_t match_case_start;
    uint16_t generic_count;
    uint32_t match_code_start;
    uint32_t match_value_spot;
    uint32_t block_type;
    uint32_t save_register_spot;

    lily_var *var_start;
    lily_var *function_var;
    lily_storage *storage_start;
    lily_class *class_entry;
    lily_storage *self;

    lily_type *match_input_type;

    struct lily_block_ *next;
    struct lily_block_ *prev;
} lily_block;

/* This is used by the emitter to do dynamic loads (ex: "abc".concat(...)). */
struct lily_parse_state_t;

typedef struct {
    int *patches;
    int *match_cases;
    lily_type **type_stack;

    uint16_t patch_pos;
    uint16_t patch_size;

    uint16_t match_case_pos;
    uint16_t match_case_size;

    uint16_t type_stack_pos;
    uint16_t type_stack_size;

    uint32_t current_generic_adjust;

    lily_var *top_var;
    lily_function_val *top_function;
    lily_type *top_function_ret;

    lily_class *current_class;
    lily_storage *self_storage;

    lily_storage *all_storage_start;
    lily_storage *unused_storage_start;
    lily_storage *all_storage_top;

    lily_block *current_block;

    uint32_t function_depth;
    uint32_t expr_num;

    uint16_t *lex_linenum;

    lily_raiser *raiser;
    lily_ast_str_pool *oo_name_pool;
    struct lily_parse_state_t *parser;
    lily_symtab *symtab;
} lily_emit_state;

# define BLOCK_IF         0x00001
# define BLOCK_IF_ELIF    0x00002
# define BLOCK_IF_ELSE    0x00004
# define BLOCK_ANDOR      0x00010
# define BLOCK_WHILE      0x00020
# define BLOCK_DO_WHILE   0x00040
# define BLOCK_FOR_IN     0x00100
# define BLOCK_TRY        0x00200
# define BLOCK_TRY_EXCEPT 0x00400
# define BLOCK_CLASS      0x01000
# define BLOCK_MATCH      0x02000
# define BLOCK_LAMBDA     0x04000
# define BLOCK_FUNCTION   0x10000

void lily_emit_eval_condition(lily_emit_state *, lily_ast_pool *);
void lily_emit_eval_expr_to_var(lily_emit_state *, lily_ast_pool *,
        lily_var *);
void lily_emit_eval_expr(lily_emit_state *, lily_ast_pool *);
void lily_emit_finalize_for_in(lily_emit_state *, lily_var *, lily_var *,
        lily_var *, lily_var *, int);
void lily_emit_eval_lambda_body(lily_emit_state *, lily_ast_pool *, lily_type *,
        int);
void lily_emit_lambda_dispatch(lily_emit_state *, lily_ast_pool *);
lily_type *lily_resolve_type(lily_emit_state *emit, lily_type *);

void lily_emit_eval_match_expr(lily_emit_state *, lily_ast_pool *);
int lily_emit_add_match_case(lily_emit_state *, int);
void lily_emit_variant_decompose(lily_emit_state *, lily_type *);

void lily_emit_break(lily_emit_state *);
void lily_emit_continue(lily_emit_state *);
void lily_emit_return(lily_emit_state *, lily_ast *);

void lily_emit_change_block_to(lily_emit_state *emit, int);
void lily_emit_enter_block(lily_emit_state *, int);
void lily_emit_leave_block(lily_emit_state *);

void lily_emit_try(lily_emit_state *, int);
void lily_emit_except(lily_emit_state *, lily_class *, lily_var *, int);
void lily_emit_raise(lily_emit_state *, lily_ast *);

void lily_emit_update_function_block(lily_emit_state *, lily_class *, int,
        lily_type *);

void lily_emit_vm_return(lily_emit_state *);
void lily_reset_main(lily_emit_state *);

void lily_update_call_generics(lily_emit_state *, int);

void lily_free_emit_state(lily_emit_state *);
int lily_emit_try_enter_main(lily_emit_state *, lily_var *);
lily_emit_state *lily_new_emit_state(lily_raiser *);

#endif
