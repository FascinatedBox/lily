#include <stdlib.h>

#include "lily_ast.h"
#include "lily_impl.h"

lily_ast *lily_ast_init_call(lily_symbol *s)
{
    lily_ast *a = malloc(sizeof(lily_ast));
    if (a == NULL)
        lily_impl_fatal("Out of memory for ast node.\n");

    a->expr_type = func_call;
    a->data.call.sym = s;
    a->data.call.args = NULL;
}

void lily_ast_add_arg(lily_ast *func, lily_ast *tree)
{
    struct lily_ast_list *l = malloc(sizeof(struct lily_ast_list));
    if (l == NULL)
        lily_impl_fatal("No memory for ast list node.\n");
    l->ast = tree;
    l->next = func->data.call.args;
    func->data.call.args = l->next;
}
