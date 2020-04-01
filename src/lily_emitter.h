#ifndef LILY_EMITTER_H
# define LILY_EMITTER_H

# include "lily_raiser.h"
# include "lily_symtab.h"
# include "lily_type_system.h"
# include "lily_type_maker.h"
# include "lily_buffer_u16.h"
# include "lily_string_pile.h"

# define SCOPE_CLASS  0x020
# define SCOPE_DEFINE 0x040
# define SCOPE_ENUM   0x080
# define SCOPE_FILE   0x100
# define SCOPE_LAMBDA 0x200

typedef enum {
    block_class          = 0 | SCOPE_CLASS,
    block_define         = 1 | SCOPE_DEFINE,
    block_do_while       = 2,
    block_enum           = 3 | SCOPE_ENUM,
    block_file           = 4 | SCOPE_FILE,
    block_for_in         = 5,
    block_if             = 6,
    block_lambda         = 7 | SCOPE_LAMBDA,
    block_match          = 8,
    block_try            = 9,
    block_while          = 10,
} lily_block_type;

/* This block uses upvalues and thus needs a closure made. */
# define BLOCK_MAKE_CLOSURE   0x01
/* If this is set on a block, then don't warn about a function not having a
   return value at the end. */
# define BLOCK_ALWAYS_EXITS   0x02
/* The closure origin block is the outermost define or lambda block. The origin
   block is responsible for initializing upvalues for inner functions to use. If
   the origin block is a class or enum method, the self it holds is the self
   that inner functions will close over to use. */
# define BLOCK_CLOSURE_ORIGIN 0x04

/* When searching for a self to close over, stop here. This is only set on
   class methods (instance or static), enum method, and the main block. */
# define BLOCK_SELF_ORIGIN    0x08

/* The self of this function block allows `self`. */
# define SELF_KEYWORD         0x10

/* The self of this function block allows instance methods. */
# define SELF_METHOD          0x20

/* The self of this function block allows `@<prop>` accesses. */
# define SELF_PROPERTY        0x40

/* This block has done at least one branch switch already. */
# define BLOCK_HAS_BRANCH     0x100

/* This block shouldn't have any more branches. */
# define BLOCK_FINAL_BRANCH   0x200

/* Storages are used to hold values not held by vars. In most cases, storages
   hold intermediate values for an expression. The emitter attempts to reuse
   storages where it can unless the storage is locked.
   This struct has the same layout as lily_sym to allow some simplifications in
   the emitter. */
typedef struct lily_storage_ {
    void *pad1;

    /* This is always ITEM_TYPE_STORAGE. */
    uint16_t item_kind;
    /* See STORAGE_* flags. */
    uint16_t flags;
    uint16_t reg_spot;
    /* This storage's location within the closure, or (uint16_t)-1 if this
       storage is not in the closure. This is only used by the self of
       class/enum methods. */
    uint16_t closure_spot;

    lily_type *type;

    /* Each expression has a different id to prevent emitter from wrongly using
       the same storage again. */
    uint32_t expr_num;
    uint32_t pad3;
} lily_storage;

/* For simplicity, classes, conditions, definitions, modules, and so on are all
   represented by one kind of block. Make sure to use a scope block when looking
   for scope information, since non-scope blocks do not carry up-to-date scope
   information. */

typedef struct lily_block_ {
    /* All blocks: What kind of block this is (see lily_block_type). */
    uint16_t block_type;
    /* All blocks: See definitions above. */
    uint16_t flags;
    /* All blocks: This tracks the last known exit in this block from return or
       raise. If all branches of a block before the end of a function always
       exit, then there's no need to complain about a missing return. */
    uint16_t last_exit;
    /* All blocks: How many vars to drop when this block exits. */
    uint16_t var_count;

    /* All blocks: Where this block's code starts in emitter's code. Scope
       blocks use this to slice their code from emitter's code buffer. Loop
       blocks use this for 'continue'. */
    uint16_t code_start;
    /* Scope blocks: The id for the next local var or storage. Global vars get
       their id from symtab. */
    uint16_t next_reg_spot;
    /* Scope blocks: How many storages to drop when this block exits. */
    uint16_t storage_count;
    /* Scope blocks: How many pending forward definitions exist. */
    uint16_t forward_count;

    /* Non-scope blocks: These are places that need to be fixed when a future
       jump location is known. */
    uint16_t patch_start;
    /* Match blocks: Where this block starts in emitter's match cases. */
    uint16_t match_case_start;
    /* Match blocks: This is the register that the match source is in. */
    uint16_t match_reg;
    /* Define block: Where to restore generics when this block closes. */
    uint16_t generic_start;

    union {
        /* Scope blocks: The var that will receive the code when this scope is
           done. This is NULL for enum blocks which is okay because they don't
           actually write code. */
        lily_var *scope_var;

        /* Match blocks: This is the type of the match expression. */
        lily_type *match_type;
    };

    /* Scope blocks: The current class or enum when processing expressions. */
    lily_class *class_entry;

    /* Scope blocks: If this block has a self in scope, this is that self. If
       this block does not have a self, this is NULL. This is also NULL if the
       current scope has a self in a closure that hasn't been used yet. */
    lily_storage *self;

    /* Scope blocks: This points to the previous scope block. If this is the
       block for '__main__', this is NULL. */
    struct lily_block_ *prev_scope_block;

    struct lily_block_ *prev;
    struct lily_block_ *next;
} lily_block;

typedef struct lily_storage_stack_ {
    lily_storage **data;
    uint16_t start;
    uint16_t size;
    uint32_t pad;
} lily_storage_stack;

typedef struct lily_proto_stack_ {
    lily_proto **data;
    uint32_t pos;
    uint32_t size;
} lily_proto_stack;

typedef struct {
    /* Patches are spots that the block needs to remember for when the block is
       done. They're shared by all blocks, starting from the index of any
       block.
       One use of this is to make sure that the branches of an 'if' block all
       get patched to the end of the block once the end is known. */
    lily_buffer_u16 *patches;

    /* Match blocks allocate space in here, initially with 0. When a match case
       is seen, it's set to 1. This is used to make sure a case isn't seen
       twice or not seen at all. */
    int *match_cases;

    /* All code is written initially to here. When a function is done, a block
       of the appropriate size is copied from here into the function value. */
    lily_buffer_u16 *code;

    /* This is a buffer used when transforming code to build a closure. */
    lily_buffer_u16 *closure_aux_code;

    lily_buffer_u16 *closure_spots;

    uint16_t *transform_table;

    uint64_t transform_size;

    uint16_t match_case_pos;

    uint16_t match_case_size;

    uint32_t pad;

    struct lily_storage_stack_ *storages;

    struct lily_proto_stack_ *protos;

    /* This is the current block, which may or may not be a scope block. This is
       never NULL because '__main__' is created to take toplevel code from the
       first module. */
    lily_block *block;

    /* This is always the most recent block representing a scope. This block is
       where ids are handed out from. */
    lily_block *scope_block;

    uint16_t pad2;

    /* How deep the current functions are. */
    uint16_t function_depth;

    /* Each expression has a unique id so that the emitter knows not to reuse
       a given storage within an expression. */
    uint32_t expr_num;

    /* This is the current line number, at any given time. */
    uint16_t *lex_linenum;

    lily_raiser *raiser;

    /* Expressions sometimes need to hold strings, like the name of a member
       access or the body of a lambda. They go here. */
    lily_string_pile *expr_strings;

    lily_type_system *ts;

    lily_type_maker *tm;

    /* The parser is stored within the emitter so that the emitter can do
       dynamic loading of functions. */
    struct lily_parse_state_ *parser;


    /* The symtab is here so the emitter can easily create storages if it needs
       to, which is often. */
    lily_symtab *symtab;
} lily_emit_state;

lily_emit_state *lily_new_emit_state(lily_symtab *, lily_raiser *);
void lily_rewind_emit_state(lily_emit_state *);
void lily_free_emit_state(lily_emit_state *);

void lily_eval_entry_condition(lily_emit_state *, lily_expr_state *);
void lily_eval_exit_condition(lily_emit_state *, lily_expr_state *);
void lily_eval_to_loop_var(lily_emit_state *, lily_expr_state *, lily_var *);
void lily_eval_expr(lily_emit_state *, lily_expr_state *);
void lily_eval_lambda_body(lily_emit_state *, lily_expr_state *, lily_type *);
void lily_eval_optarg(lily_emit_state *, lily_ast *);
void lily_eval_return(lily_emit_state *, lily_expr_state *, lily_type *);
void lily_eval_raise(lily_emit_state *, lily_expr_state *);
void lily_eval_match(lily_emit_state *, lily_expr_state *);

void lily_emit_branch_switch(lily_emit_state *);
void lily_emit_branch_finalize(lily_emit_state *);
void lily_emit_except_switch(lily_emit_state *, lily_class *, lily_var *,
        uint16_t);
int lily_emit_try_match_switch(lily_emit_state *, lily_class *);
int lily_emit_try_match_finalize(lily_emit_state *);

void lily_emit_enter_class_block(lily_emit_state *, lily_var *);
void lily_emit_enter_define_block(lily_emit_state *, lily_var *);
void lily_emit_enter_do_while_block(lily_emit_state *);
void lily_emit_enter_enum_block(lily_emit_state *, lily_class *);
void lily_emit_enter_file_block(lily_emit_state *, lily_var *);
void lily_emit_enter_for_in_block(lily_emit_state *);
void lily_emit_enter_if_block(lily_emit_state *);
void lily_emit_enter_lambda_block(lily_emit_state *, lily_var *);
void lily_emit_enter_match_block(lily_emit_state *);
void lily_emit_enter_try_block(lily_emit_state *, uint16_t);
void lily_emit_enter_while_block(lily_emit_state *);

void lily_emit_leave_block(lily_emit_state *);
void lily_emit_leave_class_block(lily_emit_state *, uint16_t);
void lily_emit_leave_define_block(lily_emit_state *, uint16_t);
void lily_emit_leave_enum_block(lily_emit_state *);
void lily_emit_leave_import_block(lily_emit_state *, uint16_t, uint16_t);
void lily_emit_leave_lambda_block(lily_emit_state *, uint16_t);
int lily_emit_try_leave_match_block(lily_emit_state *);

void lily_emit_activate_block_self(lily_emit_state *);
void lily_emit_create_block_self(lily_emit_state *, lily_type *);
void lily_emit_capture_self(lily_emit_state *, lily_block *);
int lily_emit_can_use_self_keyword(lily_emit_state *);
int lily_emit_can_use_self_method(lily_emit_state *);
int lily_emit_can_use_self_property(lily_emit_state *);

void lily_emit_write_class_init(lily_emit_state *, lily_class *, uint16_t);
void lily_emit_write_for_header(lily_emit_state *, lily_var *, lily_var *,
        lily_var *, lily_var *, uint16_t);
void lily_emit_write_shorthand_ctor(lily_emit_state *, lily_class *, lily_var *,
        uint16_t);

lily_type *lily_emit_type_for_variant(lily_emit_state *, lily_variant_class *);
void lily_emit_write_variant_case(lily_emit_state *, lily_var *, uint16_t);
void lily_emit_write_class_case(lily_emit_state *, lily_var *);

int lily_emit_try_write_break(lily_emit_state *);
int lily_emit_try_write_continue(lily_emit_state *);

lily_proto *lily_emit_new_proto(lily_emit_state *, const char *, const char *,
        const char *);
lily_proto *lily_emit_proto_for_var(lily_emit_state *, lily_var *);

void lily_prepare_main(lily_emit_state *, lily_function_val *);
void lily_clear_main(lily_emit_state *);
#endif
