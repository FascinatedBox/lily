#include "lily_ast.h"
#include "lily_impl.h"

static lily_expr_op opcode_for_token(lily_token t)
{
    lily_expr_op op;
    switch (t) {
        case tk_equal:
            op = expr_assign;
            break;
    };

    return op;
}

static lily_ast *next_pool_ast(lily_ast_pool *ap)
{
    if (ap->tree_index == ap->tree_size) {
        lily_ast **new_tree_pool;

        ap->tree_size *= 2;
        new_tree_pool = lily_realloc(ap->tree_pool,
                   sizeof(lily_ast *) * ap->tree_size);

        if (new_tree_pool == NULL)
            lily_raise_nomem(ap->error);

        ap->tree_pool = new_tree_pool;

        int i;
        for (i = ap->tree_index;i < ap->tree_size;i++) {
            ap->tree_pool[i] = lily_malloc(sizeof(lily_ast));
            if (ap->tree_pool[i] == NULL) {
                ap->tree_size = i - 1;
                lily_raise_nomem(ap->error);
            }
        }
    }

    lily_ast *ret = ap->tree_pool[ap->tree_index];
    ap->tree_index++;

    return ret;
}

static struct lily_ast_list *next_pool_list(lily_ast_pool *ap)
{
    if (ap->list_index == ap->list_size) {
        struct lily_ast_list **new_list_pool;

        ap->list_size *= 2;
        new_list_pool = lily_realloc(ap->list_pool,
                   sizeof(struct lily_ast_list *) * ap->list_size);

        if (new_list_pool == NULL)
            lily_raise_nomem(ap->error);

        ap->list_pool = new_list_pool;

        int i;
        for (i = ap->list_index;i < ap->list_size;i++) {
            ap->list_pool[i] = lily_malloc(sizeof(struct lily_ast_list));
            if (ap->list_pool[i] == NULL) {
                ap->list_size = i - 1;
                lily_raise_nomem(ap->error);
            }
        }
    }

    struct lily_ast_list *ret = ap->list_pool[ap->list_index];
    ap->list_index++;

    return ret;
}

void lily_ast_reset_pool(lily_ast_pool *ap)
{
    ap->tree_index = 0;
    ap->list_index = 0;
}

void lily_ast_free_pool(lily_ast_pool *ap)
{
    int i;

    for (i = 0;i < ap->tree_size;i++)
        lily_free(ap->tree_pool[i]);

    for (i = 0;i < ap->list_size;i++)
        lily_free(ap->list_pool[i]);

    lily_free(ap->tree_pool);
    lily_free(ap->list_pool);
    lily_free(ap);
}

lily_ast_pool *lily_ast_init_pool(lily_excep_data *excep, int pool_size)
{
    lily_ast_pool *ret;
    int listi, treei;

    ret = lily_malloc(sizeof(lily_ast_pool));
    if (ret == NULL)
        return NULL;

    ret->tree_pool = lily_malloc(sizeof(lily_ast *) * pool_size);
    ret->list_pool = lily_malloc(sizeof(struct lily_ast_list *) *
                                      pool_size);

    if (ret->tree_pool == NULL || ret->list_pool == NULL) {
        lily_free(ret->tree_pool);
        lily_free(ret->list_pool);
        lily_free(ret);
        return NULL;
    }

    for (treei = 0;treei < pool_size;treei++) {
        ret->tree_pool[treei] = lily_malloc(sizeof(lily_ast));
        if (ret->tree_pool[treei] == NULL)
            break;
    }

    for (listi = 0;listi < pool_size;listi++) {
        ret->list_pool[listi] = lily_malloc(sizeof(struct lily_ast_list));
        if (ret->list_pool[listi] == NULL)
            break;
    }

    if (listi != pool_size || treei != pool_size) {
        int j;
        for (j = 0;j < treei;j++)
            lily_free(ret->tree_pool[j]);
        for (j = 0;j < listi;j++)
            lily_free(ret->list_pool[j]);

        lily_free(ret->tree_pool);
        lily_free(ret->list_pool);
        lily_free(ret);
        return NULL;
    }

    ret->error = excep;
    ret->tree_index = 0;
    ret->tree_size = pool_size;
    ret->list_index = 0;
    ret->list_size = pool_size;
    return ret;
}

lily_ast *lily_ast_init_call(lily_ast_pool *ap, lily_symbol *s)
{
    lily_ast *a = next_pool_ast(ap);

    a->expr_type = func_call;
    a->data.call.sym = s;
    a->data.call.num_args = 0;
    a->data.call.args = NULL;
    return a;
}

lily_ast *lily_ast_init_var(lily_ast_pool *ap, lily_object *o)
{
    lily_ast *a = next_pool_ast(ap);

    a->expr_type = var;
    a->data.object = o;
    return a;
}

lily_ast *lily_ast_init_binary_op(lily_ast_pool *ap, lily_token t)
{
    lily_ast *a = next_pool_ast(ap);

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

    if (current == NULL) {
        /* new_ast is the left value. */
        ret = new_ast;
    }
    else if (current->expr_type == var ||
        current->expr_type == func_call) {
        /* current = value, new = binary op. Binary op becomes parent. */
        new_ast->data.bin_expr.left = current;
        ret = new_ast;
    }
    else {
        /* current = op, new = value or binary op. */
        if (new_ast->expr_type == var || new_ast->expr_type == func_call) {
            current->data.bin_expr.right = new_ast;
            ret = current;
        }
        else
            /* todo: Figure out two-tree merge. */
            ret = NULL;
    }

    return ret;
}

void lily_ast_add_arg(lily_ast_pool *ap, lily_ast *func, lily_ast *tree)
{
    /* fixme: This starts from the last arg and goes to the first arg, so trees
       get walked backwards and args emitted in the wrong order. Fix this when
       there's a function that needs 2+ args. */
    struct lily_ast_list *l = next_pool_list(ap);

    l->ast = tree;
    l->next = func->data.call.args;
    func->data.call.num_args++;
    func->data.call.args = l;
}
