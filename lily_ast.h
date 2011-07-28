#ifndef LILY_AST_H
# define LILY_AST_H

#include "lily_types.h"

/* Based off of http://lambda.uta.edu/cse5317/notes/node25.html. */
struct lily_ast_list;

typedef struct lily_ast_ {
    enum {
        func_call, var
    } expr_type;
    int reg_pos;
    union {
        lily_symbol *value;
        struct {
            lily_symbol *sym;
            int num_args;
            struct lily_ast_list *args;
        } call;
    } data;
} lily_ast;

struct lily_ast_list {
    lily_ast *ast;
    struct lily_ast_list *next;
};

lily_ast *lily_ast_init_call(lily_symbol *);
lily_ast *lily_ast_init_var(lily_symbol *);
void lily_ast_add_arg(lily_ast *, lily_ast *);

#endif
