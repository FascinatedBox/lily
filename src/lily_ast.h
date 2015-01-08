#ifndef LILY_AST_H
# define LILY_AST_H

# include <stdint.h>

# include "lily_raiser.h"
# include "lily_core_types.h"
# include "lily_expr_op.h"
# include "lily_membuf.h"

typedef enum {
    tree_call, tree_subscript, tree_list, tree_hash, tree_parenth,
    tree_local_var, tree_readonly, tree_global_var, tree_package,
    tree_oo_access, tree_unary, tree_type, tree_typecast, tree_tuple,
    tree_property, tree_variant, tree_lambda, tree_binary
} lily_tree_type;

typedef struct lily_ast_t {
    lily_sym *result;

    lily_tree_type tree_type : 8;
    lily_expr_op op : 8;
    uint16_t line_num;
    uint16_t args_collected;

    /* If this tree has some text data associated with it, then that data can
       be gotten from the ast pool's membuf at this position. */
    uint16_t membuf_pos;

    /* Literals and readonly functions store their initial value here. */
    lily_sym *original_sym;
    lily_prop_entry *property;

    /* Nothing uses both of these at the same time. */
    union {
        struct lily_ast_t *arg_start;
        struct lily_ast_t *left;
    };

    union {
        lily_type *type;
        /* If tree_oo_access looks up a property, then it stores the index of that
           property here. This is used by oo assign to prevent two lookups of the
           same property. */
        int oo_property_index;
        lily_class *variant_class;
        int priority;
    };

    /* These are for unary and binary ops mostly. Typecast stores a value in
       right so that operations will use that like with binary. */
    struct lily_ast_t *right;
    struct lily_ast_t *parent;

    /* If this tree is an argument, the next one. NULL otherwise. */
    struct lily_ast_t *next_arg;

    /* This links all trees together so the ast can blast them all at the end. */
    struct lily_ast_t *next_tree;
} lily_ast;

typedef struct lily_ast_freeze_entry_t {
    struct lily_ast_freeze_entry_t *next;
    struct lily_ast_freeze_entry_t *prev;

    struct lily_ast_save_entry_t *save_chain;
    lily_ast *available_restore;
    lily_ast *active;
    lily_ast *root;
    uint16_t membuf_start;
    uint16_t save_depth;

    uint32_t in_use;
} lily_ast_freeze_entry;

/* The ast handles subexpressions by merging the new tree, then storing the
   current and root of the ast pool. The save chain keeps track of what trees
   have been entered. */
typedef struct lily_ast_save_entry_t {
    /* This is a link to a newer entry, or NULL if this entry is the most
       recent. */
    struct lily_ast_save_entry_t *next;
    /* This is a link to an older entry, or NULL if this entry is the first. */
    struct lily_ast_save_entry_t *prev;
    /* This is the tree that was active before ->entered_tree was entered. */
    lily_ast *active_tree;
    /* This is the tree that was the root before ->entered_tree was entered. */
    lily_ast *root_tree;
    /* This is the tree that is taking arguments. */
    lily_ast *entered_tree;
} lily_ast_save_entry;

typedef struct {
    lily_ast *available_start;
    lily_ast *available_restore;
    lily_ast *available_current;
    lily_ast *root;
    lily_ast *active;

    /* This membuf holds two kinds of things:
       * The name to lookup when doing a dot access (ex: The 'y' of x.y).
       * The body of a lambda. */
    lily_membuf *ast_membuf;

    /* This goes from oldest (prev), to newest (next). The save_chain is always
       updated to the most recent entry when a tree is entered. */
    lily_ast_save_entry *save_chain;

    uint32_t membuf_start;
    uint32_t save_depth;

    /* When the parser enters a lambda, it 'freezes' the current tree setup in
       one of these entries until the lambda has exited. */
    lily_ast_freeze_entry *freeze_chain;

    lily_raiser *raiser;
    uint16_t *lex_linenum;
} lily_ast_pool;

void lily_ast_collect_arg(lily_ast_pool *);
void lily_ast_enter_tree(lily_ast_pool *, lily_tree_type);
void lily_free_ast_pool(lily_ast_pool *);
lily_ast_pool *lily_new_ast_pool(lily_raiser *, int);
void lily_ast_leave_tree(lily_ast_pool *);
lily_tree_type lily_ast_caller_tree_type(lily_ast_pool *);
lily_ast *lily_ast_get_saved_tree(lily_ast_pool *);
void lily_ast_enter_typecast(lily_ast_pool *ap, lily_type *type);
void lily_ast_push_local_var(lily_ast_pool *, lily_var *);
void lily_ast_push_binary_op(lily_ast_pool *, lily_expr_op);
void lily_ast_push_global_var(lily_ast_pool *, lily_var *);
void lily_ast_push_readonly(lily_ast_pool *, lily_sym *);
void lily_ast_push_unary_op(lily_ast_pool *, lily_expr_op);
void lily_ast_push_oo_access(lily_ast_pool *, char *);
void lily_ast_push_property(lily_ast_pool *, lily_prop_entry *);
void lily_ast_push_variant(lily_ast_pool *, lily_class *);
void lily_ast_push_lambda(lily_ast_pool *, int, char *);
void lily_ast_reset_pool(lily_ast_pool *);

void lily_ast_freeze_state(lily_ast_pool *);
void lily_ast_thaw_state(lily_ast_pool *);

#endif
