#ifndef LILY_EXPR_H
# define LILY_EXPR_H

# include <stdint.h>

# include "lily_core_types.h"

typedef enum {
    expr_plus,
    expr_minus,
    expr_eq_eq,
    expr_lt,
    expr_lt_eq,
    expr_gr,
    expr_gr_eq,
    expr_not_eq,
    expr_modulo,
    expr_multiply,
    expr_divide,
    expr_left_shift,
    expr_right_shift,
    expr_bitwise_and,
    expr_bitwise_or,
    expr_bitwise_xor,
    expr_unary_not,
    expr_unary_minus,
    expr_logical_and,
    expr_logical_or,
    expr_func_pipe,
    expr_assign,
    expr_plus_assign,
    expr_minus_assign,
    expr_modulo_assign,
    expr_mul_assign,
    expr_div_assign,
    expr_left_shift_assign,
    expr_right_shift_assign
} lily_expr_op;

typedef enum {
    tree_call, tree_subscript, tree_list, tree_hash, tree_parenth,
    tree_local_var, tree_defined_func, tree_global_var, tree_oo_access,
    tree_unary, tree_type, tree_typecast, tree_tuple, tree_property,
    tree_variant, tree_lambda, tree_literal, tree_inherited_new, tree_method,
    tree_static_func, tree_self, tree_upvalue, tree_interp_top,
    tree_interp_block, tree_boolean, tree_integer, tree_binary
} lily_tree_type;

typedef struct lily_ast_ {
    lily_sym *result;

    lily_tree_type tree_type: 8;
    lily_expr_op op: 8;
    uint8_t priority;
    uint8_t pad;

    uint32_t line_num;
    /* Most opcodes will write the result down at the very end. For those that
       do not, this is the code position where that result is. */
    uint16_t maybe_result_pos;
    uint16_t args_collected;
    union {
        uint32_t pile_pos;
        /* For raw integers or booleans, this is the value to write to the
           bytecode. */
        int16_t backing_value;
        /* For other kinds of literals, this is their register spot. */
        uint16_t literal_reg_spot;
    };

    union {
        lily_item *item;
        lily_sym *sym;
        lily_tie *literal;
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

    uint16_t pad;

    uint32_t *lex_linenum;

    /* The expression state to restore to when this one is done. */
    struct lily_expr_state_ *prev;
} lily_expr_state;

void lily_es_collect_arg(lily_expr_state *);
void lily_es_enter_tree(lily_expr_state *, lily_tree_type);
void lily_free_expr_state(lily_expr_state *);
lily_expr_state *lily_new_expr_state(void);
void lily_es_leave_tree(lily_expr_state *);
lily_ast *lily_es_get_saved_tree(lily_expr_state *);
void lily_es_enter_typecast(lily_expr_state *ap, lily_type *type);
void lily_es_push_local_var(lily_expr_state *, lily_var *);
void lily_es_push_binary_op(lily_expr_state *, lily_expr_op);
void lily_es_push_global_var(lily_expr_state *, lily_var *);
void lily_es_push_defined_func(lily_expr_state *, lily_var *);
void lily_es_push_method(lily_expr_state *, lily_var *);
void lily_es_push_static_func(lily_expr_state *, lily_var *);
void lily_es_push_literal(lily_expr_state *, lily_type *, uint16_t);
void lily_es_push_unary_op(lily_expr_state *, lily_expr_op);
void lily_es_push_property(lily_expr_state *, lily_prop_entry *);
void lily_es_push_variant(lily_expr_state *, lily_variant_class *);
void lily_es_push_text(lily_expr_state *, lily_tree_type, uint32_t, int);
void lily_es_push_inherited_new(lily_expr_state *, lily_var *);
void lily_es_push_self(lily_expr_state *);
void lily_es_push_upvalue(lily_expr_state *, lily_var *);
void lily_es_push_integer(lily_expr_state *, int16_t);
void lily_es_push_boolean(lily_expr_state *, int16_t);

#endif
