#include "lily_ast.h"
#include "lily_impl.h"

static lily_expr_op opcode_for_token(lily_tok_type t)
{
    lily_expr_op op;
    switch (t) {
        case tk_equal:
            op = expr_assign;
            break;
        default:
            lily_impl_fatal("Invalid token for opcode: %d.\n", t);
    };

    return op;
}

lily_ast *lily_ast_init_call(lily_symbol *s)
{
    lily_ast *a = lily_impl_malloc(sizeof(lily_ast));

    a->expr_type = func_call;
    a->data.call.sym = s;
    a->data.call.num_args = 0;
    a->data.call.args = NULL;
    return a;
}

lily_ast *lily_ast_init_var(lily_symbol *s)
{
    lily_ast *a = lily_impl_malloc(sizeof(lily_ast));

    a->expr_type = var;
    a->data.value = s;
    return a;
}

lily_ast *lily_ast_init_binary_op(lily_tok_type t)
{
    lily_ast *a = lily_impl_malloc(sizeof(lily_ast));

    a->expr_type = binary;
    a->data.bin_expr.op = opcode_for_token(t);
    a->data.bin_expr.left = NULL;
    a->data.bin_expr.right = NULL;
    a->data.bin_expr.parent = NULL;
    return a;
}

lily_ast *lily_ast_merge_trees(lily_ast *current, lily_ast *new_ast)
{
    lily_ast *ret;

    if (current->expr_type == var ||
        current->expr_type == func_call) {
        /* current = value, new = binary op. Binary op becomes parent. */
        new_ast->data.bin_expr.left = current;
        ret = new_ast;
    }
    else
        lily_impl_fatal("lily_ast_merge_trees: Handle case of current=tree.\n");

    return ret;
}

void lily_ast_add_arg(lily_ast *func, lily_ast *tree)
{
    /* fixme: This starts from the last arg and goes to the first arg, so trees
       get walked backwards and args emitted in the wrong order. Fix this when
       there's a function that needs 2+ args. */
    struct lily_ast_list *l = lily_impl_malloc(sizeof(struct lily_ast_list));

    l->ast = tree;
    l->next = func->data.call.args;
    func->data.call.num_args++;
    func->data.call.args = l;
}
