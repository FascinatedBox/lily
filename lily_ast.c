#include "lily_ast.h"
#include "lily_impl.h"

static void oo_merge(lily_ast_pool *ap, lily_ast *active, lily_ast *new_ast)
{
    lily_ast *target;

    if (active->expr_type < binary) {
        /* This gets called for two cases:
           1) a.concat("c") where a is active and root.
           2) a.concat(b.concat("c")) where b is active, but a is root.
           If this current var isn't the root, then some previous call could be
           root, so don't become root. */
        if (ap->root == ap->active)
            ap->root = new_ast;

        /* The call becomes active because it's taking over the var. Otherwise,
           lily_ast_enter_call will think the var is the parent, and make the
           var the current when the call is done. That's bad. */
        ap->active = new_ast;
        target = active;
    }
    else {
        /* This gets called when the merge is against the rhs of a binary.
           Ex: 'a = b.concat("c") < b.concat("d")
           This is always against the rhs, like how values always add to the
           rhs of a binary op. This cannot become current or root, because the
           binary always has priority over it. */
        target = active->right;
        active->right = new_ast;
    }

    new_ast->arg_start = target;
    new_ast->arg_top = target;
    new_ast->args_collected = 1;
    new_ast->next_arg = NULL;
}

static void unary_merge(lily_ast_pool *ap, lily_ast *active, lily_ast *new_ast)
{
    /* 'a = ', so there's no value for the right side...yet. */
    if (active->expr_type == binary && active->right == NULL)
        active->right = new_ast;
    else {
        /* Might be 'a = -', so there's already at least 1 unary value. */
        if (active->expr_type == binary)
            active = active->right;

        /* Unary ops are right->left (opposite of binary), and all have the same
           precedence. So values, calls, and even other unary ops will have to
           walk down to become the child of the lowest unary op. */
        while (active->expr_type == unary && active->left != NULL)
            active = active->left;

        active->left = new_ast;
    }

    new_ast->parent = active;
}

static void merge_val(lily_ast_pool *ap, lily_ast *new_ast)
{
    lily_ast *active = ap->active;

    if (active != NULL) {
        /* It's an oo call if we're merging a call against an existing
           value. */
        if (active->expr_type == binary) {
            if (active->right == NULL)
                active->right = new_ast;
            else if (active->right->expr_type == unary)
                unary_merge(ap, active, new_ast);
            else
                oo_merge(ap, active, new_ast);
        }
        else if (active->expr_type != unary)
            oo_merge(ap, active, new_ast);
        /* This happens for expressions like !!0. Make sure to do unary merge,
           especially if a binary op has already been seen. Otherwise, oo
           merge will cause part of the expression to vanish. */
        else
            unary_merge(ap, active, new_ast);
    }
    else {
        /* If no root, then no value or call so far. So become root, if only
           temporarily. */
        if (ap->root == NULL)
            ap->root = new_ast;

        ap->active = new_ast;
    }
}

static lily_ast *next_pool_ast(lily_ast_pool *ap)
{
    if (ap->tree_index == ap->tree_size) {
        lily_ast **new_tree_pool;

        new_tree_pool = lily_realloc(ap->tree_pool,
                   sizeof(lily_ast *) * ap->tree_size * 2);

        if (new_tree_pool == NULL)
            lily_raise_nomem(ap->error);

        ap->tree_size *= 2;
        ap->tree_pool = new_tree_pool;

        int i;
        for (i = ap->tree_index;i < ap->tree_size;i++) {
            ap->tree_pool[i] = lily_malloc(sizeof(lily_ast));
            if (ap->tree_pool[i] == NULL) {
                ap->tree_size = i+1;
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
        case expr_logical_or:
            prio = 1;
            break;
        case expr_logical_and:
            prio = 2;
            break;
        case expr_eq_eq:
        case expr_not_eq:
            prio = 3;
            break;
        case expr_lt:
        case expr_gr:
        case expr_lt_eq:
        case expr_gr_eq:
            prio = 4;
            break;
        case expr_plus:
        case expr_minus:
            prio = 5;
            break;
        case expr_unary_not:
        case expr_unary_minus:
            prio = 6;
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
    a->args_collected = 0;
    a->arg_start = NULL;
    a->arg_top = NULL;

    merge_val(ap, a);

    /* This is used to save the current value. If this call will be current,
       then parent will be set to NULL properly later.
       The emitter checks this to see if the call returns to anything. */
    a->parent = ap->active;

    if (ap->save_index + 2 >= ap->save_size) {
        ap->save_size *= 2;
        lily_ast **new_saved;
        new_saved = lily_realloc(ap->saved_trees, sizeof(lily_ast *) *
                                 ap->save_size);

        if (new_saved == NULL)
            lily_raise_nomem(ap->error);

        ap->saved_trees = new_saved;
    }

    ap->saved_trees[ap->save_index] = ap->root;
    ap->saved_trees[ap->save_index+1] = a;
    ap->root = NULL;
    ap->active = NULL;

    ap->save_index += 2;
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
        for (;i != 0;i--)
            lily_free(ret->tree_pool[i-1]);

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

    /* Calls with 0 args have no value, so tree is null. */
    if (tree) {
        tree->next_arg = NULL;
        call->args_collected++;
    }
}

inline void lily_ast_collect_arg(lily_ast_pool *ap)
{
    /* This is where the function is. Don't drop the index, because it's not
       done yet. */
    lily_ast *a = ap->saved_trees[ap->save_index-1];

    push_call_arg(ap, a, ap->root);

    /* Keep all of the expressions independent. */
    ap->root = NULL;
    ap->active = NULL;
}

void lily_ast_pop_tree(lily_ast_pool *ap)
{
    ap->save_index--;
    lily_ast *a = ap->saved_trees[ap->save_index];
    lily_var *v = (lily_var *)a->result;
    int args_needed = v->sig->node.call->num_args;
    push_call_arg(ap, a, ap->root);

    /* Func arg pushing doesn't check as it goes along, because that
       wouldn't handle the case of too few args. But now the function is
       supposed to be complete so... */
    if (args_needed != a->args_collected) {
        char *errstr;

        if (v->sig->node.call->is_varargs)
            errstr = "at least ";
        else
            errstr = "";

        /* Allow for -extra- args, but -only- if it's var args. */
        if ((a->args_collected > args_needed &&
              v->sig->node.call->is_varargs) == 0) {
            ap->error->line_adjust = a->line_num;
            lily_raise(ap->error, "%s expects %s%d args, got %d.\n",
                       ((lily_var *)a->result)->name, errstr, args_needed,
                       a->args_collected);
        }
    }
    ap->save_index--;
    ap->root = ap->saved_trees[ap->save_index];
    ap->active = a->parent;

    /* Current gets saved to a->parent when making a call. In some cases, the
       call was to be current, which makes the ast think it is the parent of
       itself. */
    if (a->parent == a)
        a->parent = NULL;
}

void lily_ast_push_binary_op(lily_ast_pool *ap, lily_expr_op op)
{
    lily_ast *new_ast = next_pool_ast(ap);
    lily_ast *active = ap->active;

    new_ast->expr_type = binary;
    new_ast->line_num = *ap->lex_linenum;
    new_ast->priority = priority_for_op(op);
    new_ast->op = op;
    new_ast->left = NULL;
    new_ast->right = NULL;
    new_ast->parent = NULL;

    /* Active is always non-NULL, because binary always comes after a value of
       some kind. */
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
            if (active->left->expr_type == binary &&
                active->priority < new_prio) {
                active->right = active->left;
                active->left = new_ast;
            }
            else
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
                /* Remember to operate on the tree, not current. */
                tree->parent = new_ast;
                ap->root = new_ast;
            }
            new_ast->left = tree;
            ap->active = new_ast;
        }
    }
}

void lily_ast_push_unary_op(lily_ast_pool *ap, lily_expr_op op)
{
    lily_ast *a = next_pool_ast(ap);
    lily_ast *active = ap->active;

    a->left = NULL;
    a->expr_type = unary;
    a->priority = priority_for_op(op);
    a->op = op;

    if (active != NULL) {
        if (active->expr_type == var || active->expr_type == call) {
            active->parent = a;
            ap->active = a;
            ap->root = a;
        }
        else
            unary_merge(ap, active, a);
    }
    else {
        ap->active = a;
        ap->root = a;
    }
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

    merge_val(ap, a);
}

void lily_ast_reset_pool(lily_ast_pool *ap)
{
    ap->tree_index = 0;
    ap->root = NULL;
    ap->active = NULL;
}
