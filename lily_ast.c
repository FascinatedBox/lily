#include <stdlib.h>

#include "lily_ast.h"
#include "lily_impl.h"

lily_ast *lily_ast_init_call(lily_symbol *s)
{
    lily_ast *a = lily_impl_malloc(sizeof(lily_ast));

    a->expr_type = func_call;
    a->data.call.sym = s;
    a->data.call.args = NULL;
}

void lily_ast_add_arg(lily_ast *func, lily_ast *tree)
{
    struct lily_ast_list *l = lily_impl_malloc(sizeof(struct lily_ast_list));

    l->ast = tree;
    l->next = func->data.call.args;
    func->data.call.args = l->next;
}
