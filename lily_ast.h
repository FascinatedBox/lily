#ifndef LILY_AST_H
# define LILY_AST_H

# include "lily_symtab.h"
# include "lily_expr_op.h"

/* Based off of http://lambda.uta.edu/cse5317/notes/node25.html. */
typedef struct lily_ast_t {
    enum {
        func_call, var, binary
    } expr_type;
    lily_sym *result;
    struct lily_ast_t *next_arg;
    union {
        /* Vars will store their value to result, so the emitter doesn't have
           to walk the tree for the value. */
        struct {
            lily_var *var;
            int args_collected;
            struct lily_ast_t *arg_start;
            struct lily_ast_t *arg_top;
        } call;
        struct lily_bin_expr {
            int priority;
            lily_expr_op op;
            struct lily_ast_t *left;
            struct lily_ast_t *right;
            struct lily_ast_t *parent;
        } bin_expr;
    } data;
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
} lily_ast_pool;

void lily_ast_add_arg(lily_ast_pool *, lily_ast *, lily_ast *);
void lily_ast_enter_func(lily_ast_pool *, lily_var *);
void lily_ast_free_pool(lily_ast_pool *);
lily_ast_pool *lily_ast_init_pool(lily_excep_data *, int);
void lily_ast_pop_tree(lily_ast_pool *);
void lily_ast_push_binary_op(lily_ast_pool *, lily_expr_op);
void lily_ast_push_sym(lily_ast_pool *, lily_sym *);
void lily_ast_reset_pool(lily_ast_pool *);
void lily_save_active_ast(lily_ast_pool *);
#endif
