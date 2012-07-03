#include "lily_ast.h"
#include "lily_impl.h"

static void merge_tree(lily_ast_pool *ap, lily_ast *new_ast)
{
    lily_ast *active = ap->active;

    if (new_ast->expr_type < binary) {
        if (active != NULL) {
            if (active->expr_type == binary)
                active->right = new_ast;
        }
        else {
            /* If no root, then no value or call so far. So become root, if only
               temporarily. */
            if (ap->root == NULL)
                ap->root = new_ast;

            ap->active = new_ast;
        }
    }
    else {
        if (active->expr_type < binary) {
            /* Only a value or call so far. The binary op takes over. */
            if (ap->root == active)
                ap->root = new_ast;

            new_ast->left = active;
            ap->active = new_ast;
        }
        else if (active->expr_type == binary) {
            /* Figure out how the two trees will fit together. */
            int new_prio, active_prio;
            new_prio = new_ast->priority;
            active_prio = active->priority;
            if (new_prio > active_prio) {
                /* The new tree goes before the current one. It becomes the
                   active, but not the root. */
                new_ast->left = active->right;
                active->right = new_ast;
                new_ast->parent = active;
                ap->active = new_ast;
            }
            else {
                /* This tree goes above the current one, and above any that have
                   a priority <= to its own (<= and not < because the ops need
                   to run left-to-right). Always active, and maybe root. */
                lily_ast *tree = active;
                while (tree->parent) {
                    if (new_prio > tree->parent->priority)
                        break;
    
                    tree = tree->parent;
                }
    
                if (tree->parent != NULL) {
                    /* Think 'linked list insertion'. */
                    lily_ast *parent = tree->parent;
                    if (parent->left == tree)
                        parent->left = new_ast;
                    else
                        parent->right = new_ast;
    
                    new_ast->parent = active->parent;
                }
                else {
                    /* No need to update the parent. Just become root. */
                    active->parent = new_ast;
                    ap->root = new_ast;
                }
                new_ast->left = active;
                ap->active = new_ast;
            }
        }
    }
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

static int priority_for_op(lily_expr_op o)
{
    int prio;

    switch (o) {
        case expr_assign:
            prio = 0;
            break;
        case expr_plus:
        case expr_minus:
            prio = 1;
            break;
        case expr_lt:
        case expr_gr:
        case expr_lt_eq:
        case expr_gr_eq:
            prio = 2;
            break;
        case expr_eq_eq:
            prio = 3;
            break;
        default:
            /* Won't happen, but makes -Wall happy. */
            prio = -1;
            break;
    }

    return prio;
}

void lily_ast_enter_call(lily_ast_pool *ap, lily_var *var)
{
    lily_ast *a = next_pool_ast(ap);

    a->expr_type = call;
    a->line_num = *ap->lex_linenum;
    a->result = (lily_sym *)var;
    a->args_needed = var->sig->node.call->num_args;
    a->args_collected = 0;
    a->arg_start = NULL;
    a->arg_top = NULL;

    merge_tree(ap, a);

    /* The emitter uses this to check if a call is within anything else. Also,
       this parent becomes the current when the call is done.
       Make sure this happens after merge_tree, in case active is null (it
       happens when the expression is just a call). */
    a->parent = ap->active;

    if (ap->save_index == ap->save_size) {
        ap->save_size *= 2;
        lily_ast **new_saved;
        new_saved = lily_realloc(ap->saved_trees, sizeof(lily_ast *) *
                                 ap->save_size);

        if (new_saved == NULL)
            lily_raise_nomem(ap->error);

        ap->saved_trees = new_saved;
    }

    ap->saved_trees[ap->save_index] = a;
    ap->active = NULL;

    ap->save_index++;
}

void lily_ast_free_pool(lily_ast_pool *ap)
{
    int i;

    for (i = 0;i < ap->tree_size;i++)
        lily_free(ap->tree_pool[i]);

    lily_free(ap->saved_trees);
    lily_free(ap->tree_pool);
    lily_free(ap);
}

lily_ast_pool *lily_ast_init_pool(lily_excep_data *excep, int pool_size)
{
    lily_ast_pool *ret;
    int i;

    ret = lily_malloc(sizeof(lily_ast_pool));
    if (ret == NULL)
        return NULL;

    ret->tree_pool = lily_malloc(sizeof(lily_ast *) * pool_size);
    ret->saved_trees = lily_malloc(sizeof(lily_ast *) * pool_size);
    ret->active = NULL;
    ret->root = NULL;

    if (ret->tree_pool == NULL || ret->saved_trees == NULL) {
        lily_free(ret->tree_pool);
        lily_free(ret->saved_trees);
        lily_free(ret);
        return NULL;
    }

    for (i = 0;i < pool_size;i++) {
        ret->tree_pool[i] = lily_malloc(sizeof(lily_ast));
        if (ret->tree_pool[i] == NULL)
            break;
    }

    if (i != pool_size) {
        for (;i > 0;i--)
            lily_free(ret->tree_pool[i]);

        lily_free(ret->tree_pool);
        lily_free(ret->saved_trees);
        lily_free(ret);
        return NULL;
    }

    ret->error = excep;
    ret->tree_index = 0;
    ret->tree_size = pool_size;
    ret->save_index = 0;
    ret->save_size = pool_size;
    return ret;
}

static void push_call_arg(lily_ast_pool *ap, lily_ast *call, lily_ast *tree)
{
    /* The args of a callable are linked to themselves, with the last one
       set to NULL. This will work for multiple functions, because the
       functions would be using different ASTs. */
    if (call->arg_start == NULL) {
        call->arg_start = tree;
        call->arg_top = tree;
    }
    else {
        call->arg_top->next_arg = tree;
        call->arg_top = tree;
    }

    tree->next_arg = NULL;
    call->args_collected++;
}

inline void lily_ast_collect_arg(lily_ast_pool *ap)
{
    /* This is where the function is. Don't drop the index, because it's not
       done yet. */
    lily_ast *a = ap->saved_trees[ap->save_index-1];

    push_call_arg(ap, a, ap->active);

    /* Keep all of the expressions independent. */
    ap->active = NULL;
}

void lily_ast_pop_tree(lily_ast_pool *ap)
{
    ap->save_index--;
    lily_ast *a = ap->saved_trees[ap->save_index];

    push_call_arg(ap, a, ap->active);

    /* Func arg pushing doesn't check as it goes along, because that
       wouldn't handle the case of too few args. But now the function is
       supposed to be complete so... */
    if (a->args_needed != a->args_collected) {
        ap->error->line_adjust = a->line_num;
        lily_raise(ap->error, "%s expects %d args, got %d.\n",
                   ((lily_var *)a->result)->name,
                   a->args_needed,
                   a->args_collected);
    }
    ap->active = a->parent;
}

void lily_ast_push_binary_op(lily_ast_pool *ap, lily_expr_op op)
{
    lily_ast *a = next_pool_ast(ap);

    a->expr_type = binary;
    a->line_num = *ap->lex_linenum;
    a->priority = priority_for_op(op);
    a->op = op;
    a->left = NULL;
    a->right = NULL;
    a->parent = NULL;

    merge_tree(ap, a);
}

void lily_ast_push_sym(lily_ast_pool *ap, lily_sym *s)
{
    lily_ast *a = next_pool_ast(ap);

    /* The value is stored in result, because that's where functions and
       binary ops store the object containing the result. It allows the emitter
       to do nothing for vars. */
    a->expr_type = var;
    a->line_num = *ap->lex_linenum;
    a->result = s;

    merge_tree(ap, a);
}

void lily_ast_reset_pool(lily_ast_pool *ap)
{
    ap->tree_index = 0;
    ap->root = NULL;
    ap->active = NULL;
}
