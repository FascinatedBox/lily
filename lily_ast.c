#include "lily_ast.h"
#include "lily_impl.h"

/** How ast works
    * Every tree represents something, either an operation, or a value. Some
      trees make use of some of the trees inside of them. As an example, binary
      uses left, right, and parent to refer to the left of the expression, the
      right of it, and the expression that is above it.
    * Evaluation is done from the bottom up, and left to right. Parents are
      higher up in the tree, and go later.
    * Example: 5 + 6 * 7
          5        +        6        *       7
      ---------------------------------------------
      |   5    |  +     |  +     |  +    |  +     |
      |        | /      | / \    | / \   | / \    |
      |        |5       |5   6   |5   *  |5   *   |
      |        |        |        |   /   |   / \  |
      |        |        |        |  6    |  6   7 |
      ---------------------------------------------
    * The ast pool keeps track of two important trees: the active and the top.
      The active tree is what new values will be given to. In the above example,
      5 is initially current until + is added. * becomes current when it is
      added. The top tree is the tree that is the parent of all other trees.
      This is given by the parser to the emitter so the emitter can recurse
      through all trees.

    ast is responsible for:
    * Providing an API to parser for merging in trees. Parser is expected to
      use this right (ex: not giving 3 values to a binary op, etc.), so there is
      no extra checking in ast.
    * Keeping track of the depth of the current expression (how many calls/
      parent ops/lists there are). This is used by parser to guard against an
      expression ending early (save_index is 0 if there are no subtrees).
**/

/** ast pool init, deletion, and reset. **/
lily_ast_pool *lily_new_ast_pool(lily_raiser *raiser, int pool_size)
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

    ret->raiser = raiser;
    ret->tree_index = 0;
    ret->tree_size = pool_size;
    ret->save_index = 0;
    ret->save_size = pool_size;
    return ret;
}

void lily_free_ast_pool(lily_ast_pool *ap)
{
    int i;

    for (i = 0;i < ap->tree_index;i++) {
        if (ap->tree_pool[i]->tree_type == tree_typecast &&
            ap->tree_pool[i]->sig != NULL) {
            lily_deref_sig(ap->tree_pool[i]->sig);
        }
    }

    for (i = 0;i < ap->tree_size;i++)
        lily_free(ap->tree_pool[i]);

    lily_free(ap->saved_trees);
    lily_free(ap->tree_pool);
    lily_free(ap);
}

/* lily_ast_reset_pool
   This sets the trees in the pool as not being used so that they can be used
   again. */
void lily_ast_reset_pool(lily_ast_pool *ap)
{
    ap->tree_index = 0;
    ap->root = NULL;
    ap->active = NULL;
}

/** Common private merging functions **/
/* merge_oo
   This handles an oo merge (ex: 'concat' to 'a' after a.concat) wherein
   'active' is ap->active and new_ast is the tree to be merged in. */
static void merge_oo(lily_ast_pool *ap, lily_ast *active, lily_ast *new_ast)
{
    lily_ast *target;

    if (active->tree_type < tree_typecast) {
        /* This gets called for two cases:
           1) a.concat("c") where a is active and root.
           2) a.concat(b.concat("c")) where b is active, but a is root.
           If this current var isn't the root, then some previous call could be
           root, so don't become root. */

        if (ap->root == active)
            ap->root = new_ast;

        /* The call becomes active because it's taking over the var. Otherwise,
           lily_ast_enter_call will think the var is the parent, and make the
           var the current when the call is done. That's bad. */
        ap->active = new_ast;
        target = active;
    }
    else {
        /* This gets called when the merge is against the rhs of a binary or the
           rhs of a typecast.
           Ex: 'a = b.concat("c") < b.concat("d")
               '@(type: value[0])'   ^
                             ^

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

/* merge_unary
   This handles a unary merge wherein 'active' is ap->active and new_ast is the
   tree to be merged in. */
static void merge_unary(lily_ast_pool *ap, lily_ast *new_active, lily_ast *new_ast)
{
    lily_ast *active = new_active;
    /* 'a = ' or '@(type: ', so there's no value for the right side...yet. */
    if (active->tree_type >= tree_typecast && active->right == NULL)
        active->right = new_ast;
    else {
        /* Might be 'a = -' or '@(type: ', so there's already at least 1
           unary value. */
        if (active->tree_type >= tree_typecast)
            active = active->right;

        /* Unary ops are right->left (opposite of binary), and all have the same
           precedence. So values, calls, and even other unary ops will have to
           walk down to become the child of the lowest unary op.
           The final condition ensures that we get the lowest unary tree,
           instead of the lowest unary tree's value. This is important, because
           that lowest unary tree may need to be updated. */
        while (active->tree_type == tree_unary && active->left != NULL &&
               active->left->tree_type == tree_unary)
            active = active->left;

        if (active->left == NULL)
            active->left = new_ast;
        else if (new_ast->tree_type == tree_subscript) {
            /* Subscript is a special case because it comes after a var and
               swallows it as the first arg. */
            merge_oo(ap, active->left, new_ast);
            /* new_ast contains the tree in active->left, so update the unary
               tree... */
            active->left = new_ast;
        }
        /* todo: As of now, there are no dot calls that yield an integer value.
           However, I suspect that when that occurs, dotcall will also need to
           be here. */
    }

    new_ast->parent = active;
}

/* merge_value
   This handles merging a var, call, or parenth expression. */
static void merge_value(lily_ast_pool *ap, lily_ast *new_ast)
{
    lily_ast *active = ap->active;

    if (active != NULL) {
        /* It's an oo call if we're merging a call against an existing
           value. */

        if (active->tree_type >= tree_typecast) {
            /* It's impossible to find another typecast here because inner
               typecasts are wrapped inside of a parenth tree. */
            if (active->right == NULL)
                active->right = new_ast;
            else if (active->right->tree_type == tree_unary)
                merge_unary(ap, active, new_ast);
            else
                merge_oo(ap, active, new_ast);
        }
        else if (active->tree_type == tree_unary)
            merge_unary(ap, active, new_ast);
        else
            merge_oo(ap, active, new_ast);
    }
    else {
        /* If no root, then no value or call so far. So become root, if only
           temporarily. */
        if (ap->root == NULL)
            ap->root = new_ast;

        ap->active = new_ast;
    }
}

/** Common merge helpers **/
/* next_pool_ast
   This function returns the next usable ast in the pool. If it can't find one,
   then the pool will grow and add more asts.
   This function calls lily_raise_nomem if unable to make more asts. */
static lily_ast *next_pool_ast(lily_ast_pool *ap)
{
    if (ap->tree_index == ap->tree_size) {
        lily_ast **new_tree_pool;

        new_tree_pool = lily_realloc(ap->tree_pool,
                   sizeof(lily_ast *) * ap->tree_size * 2);

        if (new_tree_pool == NULL)
            lily_raise_nomem(ap->raiser);

        ap->tree_size *= 2;
        ap->tree_pool = new_tree_pool;

        int i;
        for (i = ap->tree_index;i < ap->tree_size;i++) {
            ap->tree_pool[i] = lily_malloc(sizeof(lily_ast));
            if (ap->tree_pool[i] == NULL) {
                ap->tree_size = i+1;
                lily_raise_nomem(ap->raiser);
            }
        }
    }

    lily_ast *ret = ap->tree_pool[ap->tree_index];
    ap->tree_index++;

    return ret;
}

/* priority_for_op
   Returns the priority of a given op. The higher the number, the more important
   that it is. This usually follow's C's precedence table. */
static int priority_for_op(lily_expr_op o)
{
    int prio;

    switch (o) {
        case expr_assign:
        case expr_div_assign:
        case expr_mul_assign:
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
        case expr_multiply:
        case expr_divide:
            prio = 6;
            break;
        case expr_unary_not:
        case expr_unary_minus:
            prio = 7;
            break;
        default:
            /* Won't happen, but makes -Wall happy. */
            prio = -1;
            break;
    }

    return prio;
}

/* push_tree_arg
   This takes the current root and adds it as an argument to the last tree that
   was entered. Call is the call, and tree is typically the root. */
static void push_tree_arg(lily_ast_pool *ap, lily_ast *call, lily_ast *tree)
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
        /* This ensures that subtrees know what they're contained in. This is
           essential for autocasts. */
        tree->parent = call;
        tree->next_arg = NULL;
        call->args_collected++;
    }
}

/** API for ast. The ast is responsible for creating and merging the trees in
    properly, instead of having the parser create the trees. Do note that trees
    are reused by setting the index to 0. Only relevant fields need to be set
    (ast will never check the arg_start of a unary op, for example). **/

/* lily_ast_collect_arg
   This function will take the pool's root and add it as an argument to the
   last tree that was entered and clear root+active for reuse. */
inline void lily_ast_collect_arg(lily_ast_pool *ap)
{
    /* This is where the function is. Don't drop the index, because it's not
       done yet. */
    lily_ast *a = ap->saved_trees[ap->save_index-1];

    push_tree_arg(ap, a, ap->root);

    /* Keep all of the expressions independent. */
    ap->root = NULL;
    ap->active = NULL;
}

/* lily_ast_enter_tree
   This begins an expression that takes comma-separated arguments.
   * Parenth: tt will be tree_parenth, var will be NULL.
   * List: tt will be tree_list, var will be NULL.
   * Call: tt will be tree_call, var will be the call var.
   * Subscript: tt will be tree_subscript, var will be NULL. */
void lily_ast_enter_tree(lily_ast_pool *ap, lily_tree_type tt, lily_var *var)
{
    lily_ast *a = next_pool_ast(ap);

    a->tree_type = tt;
    a->line_num = *ap->lex_linenum;
    a->result = (lily_sym *)var;
    a->args_collected = 0;
    a->arg_start = NULL;
    a->arg_top = NULL;

    merge_value(ap, a);

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
            lily_raise_nomem(ap->raiser);

        ap->saved_trees = new_saved;
    }

    ap->saved_trees[ap->save_index] = ap->root;
    ap->saved_trees[ap->save_index+1] = a;
    ap->root = NULL;
    ap->active = NULL;

    ap->save_index += 2;
}

/* lily_ast_leave_tree
   This takes the pool's root and adds it as an argument to the last tree that
   was entered. Emitter will check the arg count when it does type checking. */
void lily_ast_leave_tree(lily_ast_pool *ap)
{
    ap->save_index--;
    lily_ast *a = ap->saved_trees[ap->save_index];

    push_tree_arg(ap, a, ap->root);

    ap->save_index--;
    ap->root = ap->saved_trees[ap->save_index];
    ap->active = a->parent;

    /* Current gets saved to a->parent when making a call. In some cases, the
       call was to be current, which makes the ast think it is the parent of
       itself. */
    if (a->parent == a)
        a->parent = NULL;
}

/* lily_ast_push_binary_op
   This 'creates' and merges a binary op against the active tree. */
void lily_ast_push_binary_op(lily_ast_pool *ap, lily_expr_op op)
{
    lily_ast *new_ast = next_pool_ast(ap);
    lily_ast *active = ap->active;

    new_ast->tree_type = tree_binary;
    new_ast->line_num = *ap->lex_linenum;
    new_ast->priority = priority_for_op(op);
    new_ast->op = op;
    new_ast->left = NULL;
    new_ast->right = NULL;
    new_ast->parent = NULL;

    /* Active is always non-NULL, because binary always comes after a value of
       some kind. */
    if (active->tree_type < tree_binary) {
        /* Only a value or call so far. The binary op takes over. */
        if (ap->root == active)
            ap->root = new_ast;

        new_ast->left = active;
        ap->active = new_ast;
    }
    else if (active->tree_type == tree_binary) {
        /* Figure out how the two trees will fit together. */
        int new_prio, active_prio;
        new_prio = new_ast->priority;
        active_prio = active->priority;
        if (new_prio > active_prio) {
            /* The new tree goes before the current one. It becomes the
               active, but not the root. */
            new_ast->left = active->right;
            if (active->left->tree_type == tree_binary &&
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

/* lily_ast_push_unary_op
   This 'creates' and merges a unary op against the active tree. */
void lily_ast_push_unary_op(lily_ast_pool *ap, lily_expr_op op)
{
    lily_ast *a = next_pool_ast(ap);
    lily_ast *active = ap->active;

    a->left = NULL;
    a->line_num = *ap->lex_linenum;
    a->tree_type = tree_unary;
    a->priority = priority_for_op(op);
    a->op = op;

    if (active != NULL) {
        if (active->tree_type == tree_var || active->tree_type == tree_call) {
            active->parent = a;
            ap->active = a;
            ap->root = a;
        }
        else
            merge_unary(ap, active, a);
    }
    else {
        ap->active = a;
        ap->root = a;
    }
}

/* lily_ast_push_sym
   This 'creates' and merges a symbol against the active tree. */
void lily_ast_push_sym(lily_ast_pool *ap, lily_sym *s)
{
    lily_ast *a = next_pool_ast(ap);

    /* The value is stored in result, because that's where functions and
       binary ops store the object containing the result. It allows the emitter
       to do nothing for vars. */
    a->tree_type = tree_var;
    a->line_num = *ap->lex_linenum;
    a->result = s;

    merge_value(ap, a);
}

/* lily_ast_push_sig
   This 'creates' a typecast tree, and places a sig into it. The tree is merged
   against the active tree. Right is used to store the value because that allows
   typecast to share some code with binary trees in some areas. */
void lily_ast_push_sig(lily_ast_pool *ap, lily_sig *sig)
{
    lily_ast *a = next_pool_ast(ap);

    a->tree_type = tree_typecast;
    a->sig = sig;
    a->right = NULL;
    a->line_num = *ap->lex_linenum;
    a->result = NULL;

    merge_value(ap, a);
}
