#include "lily_ast.h"
#include "lily_impl.h"

lily_ast *lily_ast_init_call(lily_symbol *s)
{
    lily_ast *a = lily_impl_malloc(sizeof(lily_ast));

    a->expr_type = func_call;
    a->data.call.sym = s;
    a->data.call.num_args = 0;
    a->data.call.args = NULL;
}

lily_ast *lily_ast_init_var(lily_symbol *s)
{
    lily_ast *a = lily_impl_malloc(sizeof(lily_ast));

    a->expr_type = var;
    a->data.value = s;
}

void lily_ast_add_arg(lily_ast *func, lily_ast *tree)
{
    struct lily_ast_list *l = lily_impl_malloc(sizeof(struct lily_ast_list));

    l->ast = tree;
    l->next = func->data.call.args;
    func->data.call.num_args++;
    func->data.call.args = l;
}
