#ifndef LILY_AST_H
# define LILY_AST_H

# include "lily_raiser.h"
# include "lily_syminfo.h"
# include "lily_expr_op.h"

typedef enum {
    tree_call, tree_subscript, tree_list, tree_hash, tree_parenth,
    tree_local_var, tree_readonly, tree_var, tree_package, tree_oo_call,
    tree_unary, tree_sig, tree_typecast, tree_isnil, tree_tuple, tree_binary
} lily_tree_type;

typedef struct {
    char *str;
    int pos;
    int size;
} lily_ast_str_pool;

typedef struct lily_ast_t {
    lily_tree_type tree_type;
    int line_num;

    lily_sig *sig;
    /* This is used by the emitter to hold the result of evaluating the ast.
       Additionally, tree_var will store the var here, so that emitter doesn't
       have to do anything extra for vars.
       tree_call will store the symbol to be called, if it's known at parse
       time. For anonymous calls ( abc[0](), call()(), etc.), this is NULL and
       the first arg will be evaluated to find the value to call. */
    lily_sym *result;

    /* If this tree is an argument or subtree, this is the pointer to the next
       tree in the expression, or NULL if this tree is last. */
    struct lily_ast_t *next_arg;

    /* These three are used primarily by calls. However, some ops like parenth,
       list, and subscript operate by storing their information as 'args', since
       each value is an independent sub expression. */
    int args_collected;
    struct lily_ast_t *arg_start;
    struct lily_ast_t *arg_top;

    int oo_pool_index;

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

    /* This goes from oldest (prev), to newest (next). The save_chain is always
       updated to the most recent entry when a tree is entered. */
    lily_ast_save_entry *save_chain;
    int save_depth;

    lily_raiser *raiser;
    int *lex_linenum;
} lily_ast_pool;

void lily_ast_collect_arg(lily_ast_pool *);
void lily_ast_enter_tree(lily_ast_pool *, lily_tree_type, lily_var *);
void lily_free_ast_pool(lily_ast_pool *);
lily_ast_pool *lily_new_ast_pool(lily_raiser *, int);
void lily_ast_leave_tree(lily_ast_pool *);
lily_tree_type lily_ast_caller_tree_type(lily_ast_pool *);
lily_ast *lily_ast_get_saved_tree(lily_ast_pool *);
void lily_ast_enter_typecast(lily_ast_pool *ap, lily_sig *sig);
void lily_ast_push_local_var(lily_ast_pool *, lily_var *);
void lily_ast_push_binary_op(lily_ast_pool *, lily_expr_op);
void lily_ast_push_package(lily_ast_pool *);
void lily_ast_push_sym(lily_ast_pool *, lily_sym *);
void lily_ast_push_readonly(lily_ast_pool *, lily_sym *);
void lily_ast_push_unary_op(lily_ast_pool *, lily_expr_op);
void lily_ast_push_empty_list(lily_ast_pool *ap, lily_sig *sig);
void lily_ast_push_oo_call(lily_ast_pool *, char *);
void lily_ast_reset_pool(lily_ast_pool *);
#endif
