#ifndef LILY_AST_H
# define LILY_AST_H

# include "lily_symtab.h"
# include "lily_expr_op.h"

typedef struct lily_ast_t {
    enum {
        call, var, unary, binary
    } expr_type;

    int line_num;
    /* If the tree is a var, then this is that var. If it isn't, then the
       emitter will set this to a storage holding the result of this tree. */
    lily_sym *result;

    /* The next argument to a function, if this tree is in a function. */
    struct lily_ast_t *next_arg;

    /* For functions only. */
    int args_collected;
    struct lily_ast_t *arg_start;
    struct lily_ast_t *arg_top;

    /* There are no unary ops (yet), so only binary ops use this. */
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
    lily_excep_data *error;
    int *lex_linenum;
} lily_ast_pool;

void lily_ast_add_arg(lily_ast_pool *, lily_ast *, lily_ast *);
void lily_ast_collect_arg(lily_ast_pool *);
void lily_ast_enter_call(lily_ast_pool *, lily_var *);
void lily_ast_free_pool(lily_ast_pool *);
lily_ast_pool *lily_ast_init_pool(lily_excep_data *, int);
void lily_ast_pop_tree(lily_ast_pool *);
void lily_ast_push_binary_op(lily_ast_pool *, lily_expr_op);
void lily_ast_push_sym(lily_ast_pool *, lily_sym *);
void lily_ast_reset_pool(lily_ast_pool *);
void lily_save_active_ast(lily_ast_pool *);
#endif
