#ifndef LILY_AST_H
# define LILY_AST_H

# include "lily_lexer.h"
# include "lily_symtab.h"
# include "lily_expr_op.h"

/* There's no particular arrangement to these enums, EXCEPT for typecast and
   binary. Trees that are >= typecast will usually have their ->right value
   used instead of themselves.
   Ex: a == b.concat(c) results in the .concat stealing binary's right instead
       of the whole binary tree. */
typedef enum {
    tree_call, tree_subscript, tree_list, tree_parenth, tree_local_var,
    tree_var, tree_unary, tree_typecast, tree_binary
} lily_tree_type;

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

    /* These are for unary and binary ops mostly. Typecast stores a value in
       right so that operations will use that like with binary. */
    int priority;
    lily_expr_op op;
    struct lily_ast_t *left;
    struct lily_ast_t *right;
    struct lily_ast_t *parent;
} lily_ast;

typedef struct {
    lily_ast **saved_trees;
    lily_ast **tree_pool;
    lily_ast *root;
    lily_ast *active;
    int save_index;
    int save_size;
    int tree_index;
    int tree_size;
    lily_raiser *raiser;
    int *lex_linenum;
} lily_ast_pool;

void lily_ast_collect_arg(lily_ast_pool *);
void lily_ast_enter_tree(lily_ast_pool *, lily_tree_type, lily_var *);
void lily_free_ast_pool(lily_ast_pool *);
lily_ast_pool *lily_new_ast_pool(lily_raiser *, int);
void lily_ast_leave_tree(lily_ast_pool *);
lily_tree_type lily_ast_caller_tree_type(lily_ast_pool *);
void lily_ast_push_local_var(lily_ast_pool *, lily_var *);
void lily_ast_push_binary_op(lily_ast_pool *, lily_expr_op);
void lily_ast_push_sig(lily_ast_pool *, lily_sig *);
void lily_ast_push_sym(lily_ast_pool *, lily_sym *);
void lily_ast_push_unary_op(lily_ast_pool *, lily_expr_op);
void lily_ast_push_empty_list(lily_ast_pool *ap, lily_sig *sig);
void lily_ast_reset_pool(lily_ast_pool *);
#endif
