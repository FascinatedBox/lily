#ifndef LILY_EMITTER_H
# define LILY_EMITTER_H

# include "lily_raiser.h"
# include "lily_symtab.h"
# include "lily_type_stack.h"

typedef struct lily_block_ {
    /* This block can use storages starting from here. */
    lily_storage *storage_start;

    /* Vars declared in this block are at var_start->next. */
    lily_var *var_start;

    /* Define/class blocks: This is the bytecode will be stored. */
    lily_var *function_var;

    /* Loop blocks: The position in bytecode where the loop starts.
       For non-loop blocks, this is -1. */
    int16_t loop_start;

    /* An index where the patches for this block start off. */
    uint16_t patch_start;

    /* Define/class blocks: How many generics are available in this block. */
    uint16_t generic_count;

    /* Match blocks: The starting position in emitter's match_cases. */
    uint16_t match_case_start;

    uint32_t block_type;

    /* Match blocks: This is where the code for the match starts. Cases will
       use this to write dispatching information. */
    uint32_t match_code_start;

    /* Define/class blocks: Where the symtab's register allocation was before
       entry. */
    uint32_t save_register_spot;

    uint32_t pad;

    /* Match blocks: This sym holds a register spot and the type for helping
       with match case checking. */
    lily_sym *match_sym;

    /* The current class, or NULL if not within a class. */
    lily_class *class_entry;

    /* Where 'self' is at, or NULL if not within a class. */
    lily_storage *self;

    struct lily_block_ *next;
    struct lily_block_ *prev;
} lily_block;

/* This is used by the emitter to do dynamic loads (ex: "abc".concat(...)). */
struct lily_parse_state_t;

typedef struct {
    /* Patches are spots that the block needs to remember for when the block is
       done. They're shared by all blocks, starting from the index of any
       block.
       One use of this is to make sure that the branches of an 'if' block all
       get patched to the end of the block once the end is known. */
    int *patches;

    /* Match blocks allocate space in here, initially with 0. When a match case
       is seen, it's set to 1. This is used to make sure a case isn't seen
       twice or not seen at all. */
    int *match_cases;

    uint16_t patch_pos;

    uint16_t patch_size;

    uint16_t match_case_pos;

    uint16_t match_case_size;

    uint32_t pad;

    /* Calls complete information about generics as they go along. Their
       working information is stored in type_stack_pos + 0 to type_stack_pos +
       this adjustment. This allows subtrees to grab generic information if
       they want to. */
    uint32_t current_generic_adjust;

    /* The var that will receive the function value when the function block is
       done. */
    lily_var *top_var;

    /* The value that holds the bytecode. */
    lily_function_val *top_function;

    /* The return type of the current function. */
    lily_type *top_function_ret;

    /* Either the last-entered class, or NULL. */
    lily_class *current_class;

    /* The beginning of the linked list of storages, as well as the top. The
       middle one, unused_storage_start, is used to make sure that functions
       do not touch each other's (possibly in-use) storages. */
    lily_storage *all_storage_start;
    lily_storage *unused_storage_start;
    lily_storage *all_storage_top;

    /* The currently entered block. This is never NULL, because __main__ is
       implicitly entered before any user code. */
    lily_block *block;

    /* How deep the current functions are. */
    uint32_t function_depth;

    /* Each expression has a unique id so that the emitter knows not to reuse
       a given storage within an expression. */
    uint32_t expr_num;

    /* This is the current line number, at any given time. */
    uint16_t *lex_linenum;

    lily_raiser *raiser;

    /* The ast pool stores dot access names (ex: The y of x.y), and bodies of
       lambdas here. */
    lily_membuf *ast_membuf;

    lily_type_stack *ts;

    /* The parser is stored within the emitter so that the emitter can do
       dynamic loading of functions. */
    struct lily_parse_state_t *parser;

    /* The symtab is here so the emitter can easily create storages if it needs
       to, which is often. */
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
lily_emit_state *lily_new_emit_state(lily_symtab *, lily_raiser *);

#endif
