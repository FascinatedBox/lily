#ifndef LILY_EMITTER_H
# define LILY_EMITTER_H

# include "lily_raiser.h"
# include "lily_symtab.h"
# include "lily_type_system.h"

typedef struct lily_block_ {
    /* This block can use storages starting from here. */
    lily_storage *storage_start;

    /* Vars declared in this block are at var_start->next. */
    lily_var *var_start;

    /* Define/class blocks: This is where the instructions will be written. */
    lily_function_val *function_value;

    /* Define/class blocks: This is saved because the var has the name of the
       current function. */
    lily_var *function_var;

    /* An index where the patches for this block start off. */
    uint16_t patch_start;

    /* Define/class blocks: How many generics are available in this block. */
    uint16_t generic_count;

    /* Match blocks: The starting position in emitter's match_cases. */
    uint16_t match_case_start;

    uint16_t closed_start;

    uint32_t block_type;

    /* Functions/lambdas: The start of this thing's code within emitter's
       code block. */
    uint32_t code_start;

    /* Since emit->code holds multiple code blocks, code->pos is not usable
       for jumps as-is. Use emit->code_pos - code_jump_offset to get a jump
       position appropriate for the current function.
       This bubbles upward, and is thus always available on the current block,
       regardless of the block type. */
    uint32_t jump_offset;

    /* Match blocks: This is where the code for the match starts. Cases will
       use this to write dispatching information. */
    uint32_t match_code_start;

    /* Define/class blocks: Where the symtab's register allocation was before
       entry. */
    uint32_t next_reg_spot;

    /* Define blocks: If the block returns a value, then this is the last
       return instruction finished at. If no return instruction has been seen,
       this is -1. */
    int32_t last_return;

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

typedef struct lily_emit_call_state_ {
    struct lily_emit_call_state_ *prev;
    struct lily_emit_call_state_ *next;

    /* This is what is going to be called. It is either:
       * A storage  (ex: x[0]()
       * A var      (ex: a())
       * A property (ex: a.b() if b is a class property)
       * A variant  (ex: Some(...))
       One might notice that this is a lily_item, and not a lily_sym (item only
       has flags). This is because variants are the oddball here: They're not
       true syms (can't be passed around as bare values), and they don't
       actually get called (they become tuples instead). */
    union {
        lily_item *item;
        lily_sym *sym;
        lily_var *var;
        lily_prop_entry *property;
        lily_storage *storage;
        lily_class *variant;
    };

    /* If there is an error, then this is where information for the stack trace
       is dumped out. In most cases, this is equivalent to one of the above
       items. */
    lily_item *error_item;

    /* This is the tree that the arguments are located in. */
    lily_ast *ast;

    /* This is the type of the thing being called. It's stored apart from the
       union of stuff because variants do not store the type where syms store
       the type. */
    lily_type *call_type;

    /* This is the type that vararg elements should be. It isn't solved. */
    lily_type *vararg_elem_type;

    /* This is either where varargs start at, or ((uint16_t)-1) if the current
     * call does not take varargs. */
    uint16_t vararg_start;

    /* How many arguments have been written so far. */
    uint16_t arg_count;

    /* Lily requires that all values of an enum class (the variant classes) be
       wrapped into an enum class. If a call has an unwrapped variant (ex:
       f(Some(1)), then it needs to run a secondary pass to package up the
       variants. This is done so that the enum class the variants are put into
       has as much type info as possible. */
    uint16_t have_bare_variants;

    uint16_t pad;
} lily_emit_call_state;

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

    lily_sym **call_values;

    lily_emit_call_state *call_state;

    /* All code is written initially to here. When a function is done, a block
       of the appropriate size is copied from here into the function value. */
    uint16_t *code;

    lily_sym **closed_syms;

    uint16_t *transform_table;

    uint64_t transform_size;

    /* Where the next bit of code will be written to. It is a bug to write this
       value into code. Use 'code_real_spot' instead. */
    uint32_t code_pos;

    /* How much space is allocated for code. */
    uint32_t code_size;

    uint16_t call_values_pos;

    uint16_t call_values_size;

    uint16_t closed_pos;

    uint16_t closed_size;

    uint16_t patch_pos;

    uint16_t patch_size;

    uint16_t match_case_pos;

    uint16_t match_case_size;

    /* If there is a loop block currently entered, this is the code_start of
       that block. If there is no loop block, then this is -1. */
    int32_t loop_start;

    uint32_t pad;

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

    /* The block that __main__ is within. This is always the first block (the
       only one where prev is NULL). New global vars will use this to figure
       out their id. */
    lily_block *main_block;

    /* The deepest block that will end up yielding a function. This block is the
       one that new locals and storages get their ids from. */
    lily_block *function_block;

    /* The currently entered block. This is never NULL, because __main__ is
       implicitly entered before any user code. */
    lily_block *block;

    uint16_t lambda_depth;

    /* How deep the current functions are. */
    uint16_t function_depth;

    /* Each expression has a unique id so that the emitter knows not to reuse
       a given storage within an expression. */
    uint32_t expr_num;

    /* This is the current line number, at any given time. */
    uint32_t *lex_linenum;

    lily_raiser *raiser;

    /* The ast pool stores dot access names (ex: The y of x.y), and bodies of
       lambdas here. */
    lily_membuf *ast_membuf;

    lily_type_system *ts;

    /* The parser is stored within the emitter so that the emitter can do
       dynamic loading of functions. */
    struct lily_parse_state_ *parser;


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
/* This includes 0x20000 to set it apart from others, as well as 0x10000 so that
   it's treated like a function. It's done this way because __main__ calls a
   special __import__ function for each imported file. */
# define BLOCK_FILE       0x30000

void lily_emit_eval_condition(lily_emit_state *, lily_ast_pool *);
void lily_emit_eval_expr_to_var(lily_emit_state *, lily_ast_pool *,
        lily_var *);
void lily_emit_eval_expr(lily_emit_state *, lily_ast_pool *);
void lily_emit_finalize_for_in(lily_emit_state *, lily_var *, lily_var *,
        lily_var *, lily_var *, int);
void lily_emit_eval_lambda_body(lily_emit_state *, lily_ast_pool *, lily_type *,
        int);
void lily_emit_lambda_dispatch(lily_emit_state *, lily_ast_pool *);
void lily_emit_write_import_call(lily_emit_state *, lily_var *);
void lily_emit_write_optargs(lily_emit_state *, uint16_t *, uint16_t);

void lily_emit_eval_match_expr(lily_emit_state *, lily_ast_pool *);
int lily_emit_add_match_case(lily_emit_state *, int);
void lily_emit_variant_decompose(lily_emit_state *, lily_type *);

void lily_emit_break(lily_emit_state *);
void lily_emit_continue(lily_emit_state *);
void lily_emit_return(lily_emit_state *, lily_ast *);

void lily_emit_change_block_to(lily_emit_state *emit, int);
void lily_emit_enter_simple_block(lily_emit_state *, int);

void lily_emit_enter_block(lily_emit_state *, int);
void lily_emit_leave_block(lily_emit_state *);

void lily_emit_try(lily_emit_state *, int);
void lily_emit_except(lily_emit_state *, lily_class *, lily_var *, int);
void lily_emit_raise(lily_emit_state *, lily_ast *);

void lily_emit_update_function_block(lily_emit_state *, lily_class *, int,
        lily_type *);

void lily_prepare_main(lily_emit_state *, lily_import_entry *);
void lily_reset_main(lily_emit_state *);

void lily_update_call_generics(lily_emit_state *, int);

lily_var *lily_emit_new_scoped_var(lily_emit_state *, lily_type *, char *);
lily_var *lily_emit_new_define_var(lily_emit_state *, lily_type *, char *);
lily_var *lily_emit_new_dyna_method_var(lily_emit_state *, lily_foreign_func,
        lily_class *, lily_type *, char *);
lily_var *lily_emit_new_dyna_var(lily_emit_state *, lily_import_entry *,
        lily_type *, char *);
lily_var *lily_emit_new_dyna_define_var(lily_emit_state *, lily_foreign_func,
        lily_import_entry *, lily_type *, char *);

void lily_free_emit_state(lily_emit_state *);
void lily_emit_enter_main(lily_emit_state *);
lily_emit_state *lily_new_emit_state(lily_options *, lily_symtab *, lily_raiser *);

#endif
