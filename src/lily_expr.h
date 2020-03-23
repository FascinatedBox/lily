#ifndef LILY_EXPR_H
# define LILY_EXPR_H

# include <stdint.h>

# include "lily_core_types.h"
# include "lily_token.h"

typedef enum {
    tree_call, tree_subscript, tree_list, tree_hash, tree_parenth,
    tree_local_var, tree_defined_func, tree_global_var, tree_oo_access,
    tree_unary, tree_type, tree_typecast, tree_tuple, tree_property,
    tree_variant, tree_lambda, tree_literal, tree_inherited_new, tree_method,
    tree_static_func, tree_self, tree_upvalue, tree_boolean, tree_byte,
    tree_integer, tree_oo_cached, tree_named_call, tree_named_arg, tree_binary
} lily_tree_type;

typedef struct lily_ast_ {
    lily_sym *result;

    lily_tree_type tree_type: 8;

    union {
        lily_token op: 8;
        lily_tree_type first_tree_type: 8;
    };

    union {
        uint8_t priority;
        uint16_t call_op;
    };

    uint16_t line_num;

    /* Keyword arguments will write what position they target here. */
    uint16_t keyword_arg_pos;

    uint16_t call_source_reg;

    uint16_t args_collected;

    union {
        uint32_t pile_pos;
        /* For raw integers or booleans, this is the value to write to the
           bytecode. */
        int16_t backing_value;
        /* For other kinds of literals, this is their register spot. */
        uint16_t literal_reg_spot;
        uint16_t keep_first_call_arg;
    };

    union {
        lily_item *item;
        lily_sym *sym;
        lily_prop_entry *property;
        lily_variant_class *variant;
        struct lily_ast_ *left;
        lily_type *type;
    };

    union {
        /* For trees with subtrees, this is the first child. */
        struct lily_ast_ *arg_start;
        /* Binary: This is the right side of the operation. */
        struct lily_ast_ *right;
    };

    /* If this tree is a subexpression, then this will be set to the calling
       tree. NULL otherwise. */
    struct lily_ast_ *parent;

    /* If this tree is an argument, the next one. NULL otherwise. */
    struct lily_ast_ *next_arg;

    /* This links all trees together so the ast can blast them all at the end. */
    struct lily_ast_ *next_tree;
} lily_ast;

/* Subexpressions are handled by saving the important bits of the ast pool and
   adding +1 to the pool's save depth on entry. A -1 is applied when the entry
   leaves. */
typedef struct lily_ast_save_entry_ {
    /* This was the active tree before entry. */
    lily_ast *active_tree;

    /* This was the root tree before entry. */
    lily_ast *root_tree;

    /* This is the tree that will take the subexpressions. It may or may not
       be the active tree. */
    lily_ast *entered_tree;

    struct lily_ast_save_entry_ *next;
    struct lily_ast_save_entry_ *prev;
} lily_ast_save_entry;

/* Checkpoints are created when parser needs to run some expression without
   modifying the current one. Parser uses these when running the body of a
   lambda to make sure the current expression isn't damaged.
   Save depth isn't included because checkpoints don't happen during expression
   handling (only after). */
typedef struct lily_ast_checkpoint_entry_ {
    lily_ast *first_tree;
    uint16_t pile_start;
    uint16_t pad;
    uint32_t pad2;
    lily_ast *root;
    lily_ast *active;
} lily_ast_checkpoint_entry;

typedef struct lily_expr_state_ {
    /* This is the tree with the lowest precedence. */
    lily_ast *root;

    /* This is the tree currently being worked with. */
    lily_ast *active;

    /* This is the next available tree. */
    lily_ast *next_available;

    /* This is the first available tree in this scope. When this expression is
       done, it rewinds to this. */
    lily_ast *first_tree;

    /* The save entries that this holds. It won't be null, but it may not be in
       use. */
    lily_ast_save_entry *save_chain;

    /* How many expressions have been saved so far? If it's more than one, then
       the current expression isn't done. */
    uint16_t save_depth;

    /* Where does the string pile start for this expression? */
    uint16_t pile_start;

    /* Where should inserting to the string pile start from? */
    uint16_t pile_current;

    /* How many optarg expressions are currently saved. */
    uint16_t optarg_count;

    lily_ast_checkpoint_entry **checkpoints;
    uint32_t checkpoint_pos;
    uint32_t checkpoint_size;

    uint16_t *lex_linenum;
} lily_expr_state;

lily_expr_state *lily_new_expr_state(void);
void lily_rewind_expr_state(lily_expr_state *);
void lily_free_expr_state(lily_expr_state *);

void lily_es_flush(lily_expr_state *);
void lily_es_checkpoint_save(lily_expr_state *);
void lily_es_checkpoint_restore(lily_expr_state *);
void lily_es_optarg_save(lily_expr_state *);
void lily_es_optarg_finish(lily_expr_state *);

void lily_es_collect_arg(lily_expr_state *);
void lily_es_enter_tree(lily_expr_state *, lily_tree_type);
void lily_es_leave_tree(lily_expr_state *);
lily_ast *lily_es_get_saved_tree(lily_expr_state *);
void lily_es_enter_typecast(lily_expr_state *ap, lily_type *type);
void lily_es_push_local_var(lily_expr_state *, lily_var *);
void lily_es_push_binary_op(lily_expr_state *, lily_token);
void lily_es_push_global_var(lily_expr_state *, lily_var *);
void lily_es_push_defined_func(lily_expr_state *, lily_var *);
void lily_es_push_method(lily_expr_state *, lily_var *);
void lily_es_push_static_func(lily_expr_state *, lily_var *);
void lily_es_push_literal(lily_expr_state *, lily_type *, uint16_t);
void lily_es_push_unary_op(lily_expr_state *, lily_token);
void lily_es_push_property(lily_expr_state *, lily_prop_entry *);
void lily_es_push_variant(lily_expr_state *, lily_variant_class *);
void lily_es_push_text(lily_expr_state *, lily_tree_type, uint16_t, int);
void lily_es_push_inherited_new(lily_expr_state *, lily_var *);
void lily_es_push_self(lily_expr_state *);
void lily_es_push_upvalue(lily_expr_state *, lily_var *);
void lily_es_push_integer(lily_expr_state *, int16_t);
void lily_es_push_boolean(lily_expr_state *, int16_t);
void lily_es_push_byte(lily_expr_state *, uint8_t);
void lily_es_push_assign_to(lily_expr_state *, lily_sym *);

#endif
