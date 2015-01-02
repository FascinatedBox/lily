#ifndef LILY_AST_H
# define LILY_AST_H

# include "lily_raiser.h"
# include "lily_core_types.h"
# include "lily_expr_op.h"

typedef enum {
    tree_call, tree_subscript, tree_list, tree_hash, tree_parenth,
    tree_local_var, tree_readonly, tree_var, tree_package, tree_oo_access,
    tree_unary, tree_sig, tree_typecast, tree_tuple, tree_property,
    tree_variant, tree_lambda, tree_binary
} lily_tree_type;

typedef struct {
    char *str;
    int pos;
    int size;
} lily_ast_str_pool;

typedef struct lily_ast_t {
    lily_tree_type tree_type;
    uint16_t line_num;

    lily_sym *original_sym;
    lily_prop_entry *property;
    lily_sig *sig;
    /* This is where the result of evaluating the tree goes. This is used
       because it's a subset of both lily_var and lily_storage, either of which
       it may be set to. */
    lily_sym *result;

    /* These three are used by anything that has subtrees...which is a lot. */
    int args_collected;
    struct lily_ast_t *arg_start;
    struct lily_ast_t *next_arg;

    /* If tree_oo_access looks up a property, then it stores the index of that
       property here. This is used by oo assign to prevent two lookups of the
       same property. */
    int oo_property_index;
    int oo_pool_index;

    lily_class *variant_class;

    /* These are for unary and binary ops mostly. Typecast stores a value in
       right so that operations will use that like with binary. */
    int priority;
    lily_expr_op op;
    struct lily_ast_t *left;
    struct lily_ast_t *right;
    struct lily_ast_t *parent;

    /* All trees are linked together so that the ast pool can quickly grab the
       next unused one. */
    struct lily_ast_t *next_tree;
} lily_ast;

typedef struct lily_ast_freeze_entry_t {
    struct lily_ast_freeze_entry_t *next;
    struct lily_ast_freeze_entry_t *prev;

    struct lily_ast_save_entry_t *save_chain;
    lily_ast *active;
    lily_ast *root;
    int oo_start;
    int save_depth;
    lily_ast *available_restore;

    int in_use;
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

    /* This holds the names of calls invoked through dots. Ex: 'abc.def()'.
       Each tree_oo that uses this will have a starting index saying the string
       it's interested in starts at. An index is used so that the ast pool can
       realloc the one large string and not worry about invalidating anything.
       Each string copied in is \0 terminated.
       Ex: abc.concat("def").concat("ghi")
           Pool:   "def\0ghi\0"
           Starts:  ^    ^
       This scheme is done so that the parser does not have to guess the type
       to do the  */
    lily_ast_str_pool *oo_name_pool;
    int oo_start;

    /* This goes from oldest (prev), to newest (next). The save_chain is always
       updated to the most recent entry when a tree is entered. */
    lily_ast_save_entry *save_chain;
    int save_depth;

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
void lily_ast_enter_typecast(lily_ast_pool *ap, lily_sig *sig);
void lily_ast_push_local_var(lily_ast_pool *, lily_var *);
void lily_ast_push_binary_op(lily_ast_pool *, lily_expr_op);
void lily_ast_push_sym(lily_ast_pool *, lily_sym *);
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
