#ifndef LILY_AST_H
# define LILY_AST_H

# include <stdint.h>

# include "lily_core_types.h"
# include "lily_membuf.h"

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
    tree_variant, tree_lambda, tree_literal, tree_inherited_new, tree_self,
    tree_upvalue, tree_open_upvalue, tree_binary
} lily_tree_type;

typedef struct lily_ast_ {
    lily_sym *result;

    lily_tree_type tree_type : 16;
    lily_expr_op op : 16;
    uint32_t line_num;
    uint32_t args_collected;

    /* If this tree has some text data associated with it, then that data can
       be gotten from the ast pool's membuf at this position. */
    uint32_t membuf_pos;

    /* Since lily_item is a superset of all of the types that follow, it would
       be possible to just use lily_item alone here. However, that results in
       parts of the emitter having to cast the item to a sym or something higher
       up as a means of acquiring either a type or a name. That sucks.
       Why include lily_item then? Well, call makes use of it (since a call can
       be against a variant, a var, or a storage) by putting the thing it's
       trying to call there. Doing so allows debug information to be grabbed. */
    union {
        lily_item *item;
        lily_sym *sym;
        lily_tie *literal;
        lily_prop_entry *property;
        lily_class *variant_class;
    };

    /* Nothing uses both of these at the same time. */
    union {
        struct lily_ast_ *arg_start;
        struct lily_ast_ *left;
    };

    union {
        /* This is no longer called 'type', because of numerous prior issues
           with doing ast->type instead of ast->result->type. */
        lily_type *typecast_type;
        /* If tree_oo_access looks up a property, then it stores the index of that
           property here. This is used by oo assign to prevent two lookups of the
           same property. */
        int oo_property_index;
        int priority;
    };

    /* right is the right side of a binary operation. Unused otherwise. */
    struct lily_ast_ *right;

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

/* Save entries only handle expressions, and the parser tracks them to make
   sure that an expression does not exit before it should.
   A freeze entry is used to handle saving the entire state of the pool
   (subexpressions and all). This allows the parser to build another expression
   and run it without allowing it to wipe what currently exists.
   Lambdas are one example of where this is useful. */
typedef struct lily_ast_freeze_entry_ {
    /* Assume all of the trees that were previously used are still in use and
       do not touch them at all ever. */
    lily_ast *available_restore;

    /* Similarly, text might be associated with trees in the emitter, so make
       sure that the reset doesn't allow that to be blown away. */
    uint16_t membuf_start;

    /* Freeze entries are less common than save entries, so new entries are
       only made on-demand. 1 if in use, 0 if not. */
    uint16_t in_use;

    /* The number of saves registered before this freeze. */
    uint32_t save_depth;

    /* The root tree before the freeze. */
    lily_ast *root;

    /* The active tree before the freeze. */
    lily_ast *active;

    struct lily_ast_freeze_entry_ *next;
    struct lily_ast_freeze_entry_ *prev;
} lily_ast_freeze_entry;

typedef struct {
    /* This is the head of the linked list of trees. */
    lily_ast *available_start;

    /* When this expression is done, where to set available_current to. This is
       set to available_start UNLESS within a lambda. */
    lily_ast *available_restore;

    /* The next available tree for the pool to use. Never NULL. */
    lily_ast *available_current;

    /* The tree with the lowest precedence. This is where evaluation will start
       from. */
    lily_ast *root;

    /* This is the tree that will be acted upon (the binary merged against, the
       value to merge as an arg, etc). */
    lily_ast *active;

    /* This membuf holds two kinds of things:
       * The name to lookup when doing a dot access (ex: The 'y' of x.y).
       * The body of a lambda. */
    lily_membuf *ast_membuf;

    /* This holds bits of the pool's state when handling non-lambda tree
       entry. */
    lily_ast_save_entry *save_chain;

    /* Where the ast should restore the membuf's pos to when the expression is
       done. This is non-zero when within a lambda. */
    uint32_t membuf_start;

    /* How deep within ( or [ that this expression is. This should always be 0
       at the start and end of an expression. */
    uint32_t save_depth;

    /* This holds the ast pool's state when processing a lambda. Lambdas are
       unique in that they're dispatched from emitter while it's in an
       expression. Therefore, they can't re-use trees and such. */
    lily_ast_freeze_entry *freeze_chain;

    uint32_t *lex_linenum;
} lily_ast_pool;

void lily_ast_collect_arg(lily_ast_pool *);
void lily_ast_enter_tree(lily_ast_pool *, lily_tree_type);
void lily_free_ast_pool(lily_ast_pool *);
lily_ast_pool *lily_new_ast_pool(lily_options *, int);
void lily_ast_leave_tree(lily_ast_pool *);
lily_ast *lily_ast_get_saved_tree(lily_ast_pool *);
void lily_ast_enter_typecast(lily_ast_pool *ap, lily_type *type);
void lily_ast_push_local_var(lily_ast_pool *, lily_var *);
void lily_ast_push_binary_op(lily_ast_pool *, lily_expr_op);
void lily_ast_push_global_var(lily_ast_pool *, lily_var *);
void lily_ast_push_defined_func(lily_ast_pool *, lily_var *);
void lily_ast_push_literal(lily_ast_pool *, lily_tie *);
void lily_ast_push_unary_op(lily_ast_pool *, lily_expr_op);
void lily_ast_push_oo_access(lily_ast_pool *, char *);
void lily_ast_push_property(lily_ast_pool *, lily_prop_entry *);
void lily_ast_push_variant(lily_ast_pool *, lily_class *);
void lily_ast_push_lambda(lily_ast_pool *, int, char *);
void lily_ast_push_inherited_new(lily_ast_pool *, lily_var *);
void lily_ast_push_self(lily_ast_pool *);
void lily_ast_push_upvalue(lily_ast_pool *, lily_var *);
void lily_ast_push_open_upvalue(lily_ast_pool *, lily_var *);
void lily_ast_reset_pool(lily_ast_pool *);

void lily_ast_freeze_state(lily_ast_pool *);
void lily_ast_thaw_state(lily_ast_pool *);

#endif
