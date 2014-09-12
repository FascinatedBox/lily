#include <string.h>

#include "lily_impl.h"
#include "lily_ast.h"

/** How ast works
    * Every kind of a tree represents something different. It's always either a
      value, or an operation that needs to be performed.
    * There are two important trees: active, and root.
      * active: This is what new trees will interact with when merging.
      * root:   This is the tree that holds all others (it has no parents).
    * Binary trees use left and right. These trees have precedence which
      determines how they merge with each other. Unary trees are right to left,
      so new values always merge against the lowest value of them.
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
    * Most trees, however, are left to right with no precedence rules, and will
      simply absorb (usually the current) tree as an argument.
    * The pool stores the asts used for an expression, and reuses those on the
      next pass. The trees are linked to each other through the ->next_tree
      field. New trees are added as they are needed, so there is no waste.

    ast is responsible for:
    * Providing an API to parser for merging in trees. The ast pool does not
      perform sanity checking (parser is responsible for only allowing valid
      merges).
    * Keeping track of the depth of the current expression (how many calls/
      parent ops/lists there are). This is used by parser to guard against an
      expression ending early (save_index is 0 if there are no subtrees).
**/

/*  AST_COMMON_INIT
    This macro does common initialization for all new asts. This doesn't set
    result to NULL, because value trees set a result. */
#define AST_COMMON_INIT(a, tt) \
lily_ast *a; \
if (ap->available_current) { \
    a = ap->available_current; \
    ap->available_current = a->next_tree; \
} \
else \
    a = make_new_tree(ap); \
a->tree_type = tt; \
a->line_num = *ap->lex_linenum; \
a->parent = NULL;


/*  AST_ENTERABLE_INIT
    This macro does common initialization for trees that are meant to be
    entered (and will collect args). */
#define AST_ENTERABLE_INIT(a, tt) \
AST_COMMON_INIT(a, tt) \
a->args_collected = 0; \
a->arg_start = NULL; \
a->arg_top = NULL; \
a->result = NULL;


/** ast pool init, deletion, and reset. **/
lily_ast_pool *lily_new_ast_pool(lily_raiser *raiser, int pool_size)
{
    lily_ast_pool *ap = lily_malloc(sizeof(lily_ast_pool));
    int i, ok = 1;
    lily_ast *last_tree;

    if (ap == NULL)
        return NULL;

    ap->raiser = raiser;
    ap->active = NULL;
    ap->root = NULL;
    ap->available_start = NULL;
    ap->available_current = NULL;
    ap->oo_name_pool = NULL;

    last_tree = NULL;
    for (i = 0;i < pool_size;i++) {
        lily_ast *new_tree = lily_malloc(sizeof(lily_ast));
        if (new_tree == NULL) {
            ok = 0;
            break;
        }

        new_tree->next_tree = last_tree;
        last_tree = new_tree;
    }

    if (ok == 1) {
        ap->save_chain = lily_malloc(sizeof(lily_ast_save_entry));
        if (ap->save_chain != NULL) {
            ap->save_chain->next = NULL;
            ap->save_chain->prev = NULL;
            ap->save_chain->root_tree = NULL;
            ap->save_chain->active_tree = NULL;
        }
        else
            ok = 0;

        ap->save_depth = 0;
    }

    ap->available_start = last_tree;
    ap->available_current = last_tree;

    lily_ast_str_pool *oo_name_pool = lily_malloc(sizeof(lily_ast_str_pool));
    char *pool_str = lily_malloc(8 * sizeof(char));
    if (oo_name_pool == NULL || pool_str == NULL) {
        lily_free(oo_name_pool);
        lily_free(pool_str);
        ok = 0;
    }
    else {
        ap->oo_name_pool = oo_name_pool;
        oo_name_pool->str = pool_str;
        oo_name_pool->pos = 0;
        oo_name_pool->size = 8;
    }

    if (ok == 0) {
        lily_free_ast_pool(ap);
        return NULL;
    }

    return ap;
}

void lily_free_ast_pool(lily_ast_pool *ap)
{
    lily_ast *ast_iter = ap->available_start;
    lily_ast *ast_temp;

    while (ast_iter) {
        ast_temp = ast_iter->next_tree;

        lily_free(ast_iter);

        ast_iter = ast_temp;
    }

    /* Destroying ->save_chain is a bit tricky because it's updated for new
       entries. It could be at the beginning, the middle, or the end. So start
       off by moving it to the front. The first entry is the only one that will
       have ->prev set to NULL.
       Also, ap->save_chain may be NULL if this is called from
       lily_new_ast_pool, so watch out for that too. */
    lily_ast_save_entry *save_iter = ap->save_chain;
    if (save_iter != NULL) {
        /* First get to the very front... */
        while (save_iter->prev)
            save_iter = save_iter->prev;

        /* Then go from front to back and delete them as usual. */
        lily_ast_save_entry *save_temp;
        while (save_iter) {
            save_temp = save_iter->next;

            lily_free(save_iter);

            save_iter = save_temp;
        }
    }

    if (ap->oo_name_pool) {
        lily_free(ap->oo_name_pool->str);
        lily_free(ap->oo_name_pool);
    }

    lily_free(ap);
}

/* lily_ast_reset_pool
   This sets the trees in the pool as not being used so that they can be used
   again. */
void lily_ast_reset_pool(lily_ast_pool *ap)
{
    ap->root = NULL;
    ap->active = NULL;
    ap->oo_name_pool->pos = 0;
    ap->available_current = ap->available_start;
}

/** Common private merging functions **/
/*  merge_absorb
    This does a merge where 'given' will be swallowed by 'new_tree' as an
    argument. This is the most common merge (aside from binary). This handles
    lists (each element is an argument), typecasts (1 arg is the type, the
    other is the value to cast), calls, and much, much more.
    Note: 'given' may not be the active tree. */
static void merge_absorb(lily_ast_pool *ap, lily_ast *given, lily_ast *new_tree)
{
    /* If the swallowed tree is active/root, become those. */
    if (given == ap->active) {
        ap->active = new_tree;
        if (given == ap->root)
            ap->root = new_tree;
    }

    given->parent = new_tree;
    new_tree->arg_start = given;
    new_tree->arg_top = given;
    new_tree->args_collected = 1;
    new_tree->next_arg = NULL;
}

/* merge_unary
   This handles a unary merge wherein 'active' is ap->active and new_ast is the
   tree to be merged in. */
static void merge_unary(lily_ast_pool *ap, lily_ast *given, lily_ast *new_tree)
{
    /* Unary ops are right to left, so newer trees go lower. Descend to find
       the lowest unary tree, so that the merge will be against the value it
       holds. */
    while (given->tree_type == tree_unary && given->left != NULL &&
           given->left->tree_type == tree_unary)
        given = given->left;

    if (given->left == NULL)
        /* Either a value, or another unary tree. */
        given->left = new_tree;
    else {
        /* This won't be a unary, binary, or a value, so absorb the unary's
           value and become the new unary value. */
        merge_absorb(ap, given->left, new_tree);
        given->left = new_tree;
    }

    new_tree->parent = given;
}

/*  merge_value
    This handles merging in of any new tree. It will dispatch to other merges
    based on the current tree. In most cases, this does the correct merge.
    However, there are some cases where this won't be right (such as merging
    in a unary tree when the current is a value). */
static void merge_value(lily_ast_pool *ap, lily_ast *new_tree)
{
    lily_ast *active = ap->active;

    if (active != NULL) {
        if (active->tree_type == tree_binary) {
            /* New trees merge against binary's right. */
            if (active->right == NULL) {
                active->right = new_tree;
                new_tree->parent = active;
            }
            else if (active->right->tree_type == tree_unary)
                /* Unary merges right to left, so ->right does not have to be
                   fixed afterward. */
                merge_unary(ap, active->right, new_tree);
            else {
                /* new_tree swallows active->right, but doesn't update the
                   parent. The parent has to be updated because this is a
                   left to right merge. */
                merge_absorb(ap, active->right, new_tree);
                active->right = new_tree;
            }
        }
        else if (active->tree_type == tree_unary)
            merge_unary(ap, active, new_tree);
        else
            merge_absorb(ap, active, new_tree);
    }
    else {
        /* If there's no active, then there's no root. Become both. */
        ap->root = new_tree;
        ap->active = new_tree;
    }
}

/** Common merge helpers **/
/* make_new_tree
   Create a new tree, which becomes the start of the chain of available trees
   for the pool to use. The tree is returned as well, so it can be used.
   This raises ErrNoMem if unable to make a new tree. */
static lily_ast *make_new_tree(lily_ast_pool *ap)
{
    lily_ast *new_ast = lily_malloc(sizeof(lily_ast));
    if (new_ast == NULL)
        lily_raise_nomem(ap->raiser);

    new_ast->next_tree = ap->available_start;
    ap->available_start = new_ast;
    /* The pool will never re-use any trees that have already been used until
       the ast has been walked. Therefore, there is no need to update
       ->available_current. */

    return new_ast;
}

/*  make_new_save_entry
    This is a helper routine that will attempt to create a new
    lily_ast_save_entry.

    ap: The ast pool to put the new save entry into.

    On failure: lily_raise_nomem is called, raising ErrNoMem.
    On success: * The new entry is added to the linked list at the pool's
                  ap's ->save_chain. It then becomes the ap's ->save_chain.
                * The new entry's fields are initially set to NULL.
                * The new entry is returned for convenince. */
static lily_ast_save_entry *make_new_save_entry(lily_ast_pool *ap)
{
    lily_ast_save_entry *new_entry = lily_malloc(sizeof(lily_ast_save_entry));
    if (new_entry == NULL)
        lily_raise_nomem(ap->raiser);

    /* Must link both ways, or the pool won't be able to find this entry the
       next time it looks for it. */
    new_entry->prev      = ap->save_chain;
    ap->save_chain->next = new_entry;

    new_entry->root_tree = NULL;
    new_entry->active_tree = NULL;
    new_entry->entered_tree = NULL;
    new_entry->next = NULL;

    return new_entry;
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
        case expr_plus_assign:
        case expr_minus_assign:
        case expr_left_shift_assign:
        case expr_right_shift_assign:
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
        /* Bitwise ops are intentionally put before equality operations. This
           allows users to use bitwise ops without parens.
           This:        a & 0x10 == x
           Instead of: (a & 0x10) == x

           Keeping their different precendence levels for now though. */
        case expr_bitwise_or:
            prio = 5;
            break;
        case expr_bitwise_xor:
            prio = 6;
            break;
        case expr_bitwise_and:
            prio = 7;
            break;
        case expr_left_shift:
        case expr_right_shift:
            prio = 8;
            break;
        case expr_plus:
        case expr_minus:
            prio = 9;
            break;
        case expr_multiply:
        case expr_divide:
        case expr_modulo:
            prio = 10;
            break;
        case expr_unary_not:
        case expr_unary_minus:
            prio = 11;
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
    lily_ast_save_entry *entry = ap->save_chain;

    push_tree_arg(ap, entry->entered_tree, ap->root);

    /* Keep all of the expressions independent. */
    ap->root = NULL;
    ap->active = NULL;
}

/* lily_ast_enter_tree
   This begins an expression that takes comma-separated arguments. 'var' is only
   used by tree_call, and only when the call is a named variable. In all other
   cases, 'var' is NULL and ignored. */
void lily_ast_enter_tree(lily_ast_pool *ap, lily_tree_type tree_type,
        lily_var *var)
{
    AST_ENTERABLE_INIT(a, tree_type)
    a->result = (lily_sym *)var;

    merge_value(ap, a);

    lily_ast_save_entry *save_entry;
    if (ap->save_depth == 0)
        save_entry = ap->save_chain;
    else {
        if (ap->save_chain->next != NULL)
            save_entry = ap->save_chain->next;
        else
            save_entry = make_new_save_entry(ap);

        ap->save_chain = save_entry;
    }

    save_entry->root_tree = ap->root;
    save_entry->active_tree = ap->active;
    save_entry->entered_tree = a;
    ap->save_depth++;

    ap->root = NULL;
    ap->active = NULL;
}

/* lily_ast_caller_tree_type
   This function returns the type of tree that is currently receiving arguments.
   This is used by parser to ensure that the proper token is used to close the
   tree. */
lily_tree_type lily_ast_caller_tree_type(lily_ast_pool *ap)
{
    lily_ast_save_entry *save_entry = ap->save_chain;

    return save_entry->entered_tree->tree_type;
}

/* lily_ast_leave_tree
   This takes the pool's root and adds it as an argument to the last tree that
   was entered. Emitter will check the arg count when it does type checking. */
void lily_ast_leave_tree(lily_ast_pool *ap)
{
    lily_ast_save_entry *entry = ap->save_chain;

    push_tree_arg(ap, entry->entered_tree, ap->root);

    ap->root = entry->root_tree;
    ap->active = entry->active_tree;

    /* The first tree takes ap->save_chain's bottom and doesn't move
       ->save_chain. Further entries move it, so undo that move. */
    if (ap->save_depth > 1)
        ap->save_chain = ap->save_chain->prev;

    ap->save_depth--;
}

/*  lily_ast_get_saved_tree
    This returns the tree that was last entered. This is used by parser to
    determine if tk_arrow / tk_comma are valid.

    ap: The ast pool that the parser is using.

    Parser is responsible for ensuring that this is only called when the pool
    has entered a tree (ap->save_index != 0) before calling this. */
lily_ast *lily_ast_get_saved_tree(lily_ast_pool *ap)
{
    return ap->save_chain->entered_tree;
}

/* lily_ast_push_binary_op
   This 'creates' and merges a binary op against the active tree. */
void lily_ast_push_binary_op(lily_ast_pool *ap, lily_expr_op op)
{
    /* Call it 'new_ast', because this merge is a bit complex. */
    AST_COMMON_INIT(new_ast, tree_binary)
    new_ast->priority = priority_for_op(op);
    new_ast->op = op;
    new_ast->left = NULL;
    new_ast->right = NULL;

    lily_ast *active = ap->active;

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
        if ((new_prio > active_prio) || new_prio == 0) {
            /* The new tree goes before the current one, so it steals the rhs
               and becomes it (because lower trees have precedence). Since the
               new tree still needs a right, it becomes current.
               new_prio == 0 is so that assign and assign-like operations run
               right-to-left. */
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
                /* Remember to operate on the tree, not current. */
                tree->parent = new_ast;
                ap->root = new_ast;
            }
            new_ast->left = tree;
            ap->active = new_ast;
        }
    }
}

/* lily_ast_push_sig
   This 'creates' a sig tree for tree_typecast. This tree's purpose is to hold
   the signature that the typecast will try to coerce its value to. */
static void push_sig(lily_ast_pool *ap, lily_sig *sig)
{
    AST_COMMON_INIT(a, tree_sig)
    a->sig = sig;
    a->result = NULL;

    merge_value(ap, a);
}

void lily_ast_enter_typecast(lily_ast_pool *ap, lily_sig *sig)
{
    lily_ast_enter_tree(ap, tree_typecast, NULL);
    push_sig(ap, sig);
    lily_ast_collect_arg(ap);
}

/* lily_ast_push_unary_op
   This 'creates' and merges a unary op against the active tree. */
void lily_ast_push_unary_op(lily_ast_pool *ap, lily_expr_op op)
{
    AST_COMMON_INIT(a, tree_unary)
    a->result = NULL;
    a->left = NULL;
    a->priority = priority_for_op(op);
    a->op = op;

    lily_ast *active = ap->active;

    if (active != NULL) {
        /* Don't use merge_value, because it picks based off the active tree
           and will select an absorb merge. That's wrong for unary. */
        if (active->tree_type == tree_var ||
            active->tree_type == tree_local_var ||
            active->tree_type == tree_call ||
            active->tree_type == tree_readonly) {
            active->parent = a;
            ap->active = a;
            ap->root = a;
        }
        else
            merge_value(ap, a);
    }
    else {
        ap->active = a;
        ap->root = a;
    }
}

void lily_ast_push_local_var(lily_ast_pool *ap, lily_var *var)
{
    AST_COMMON_INIT(a, tree_local_var);
    a->result = (lily_sym *)var;

    merge_value(ap, a);
}

/* lily_ast_push_sym
   This creates and merges a symbol holding a value. This symbol can be either
   a literal, or a global var.
   These are separated from local vars because literals and globals need to be
   loaded into a register before use. */
void lily_ast_push_sym(lily_ast_pool *ap, lily_sym *s)
{
    AST_COMMON_INIT(a, tree_var);
    a->result = s;

    merge_value(ap, a);
}

void lily_ast_push_readonly(lily_ast_pool *ap, lily_sym *ro_sym)
{
    AST_COMMON_INIT(a, tree_readonly);
    a->result = ro_sym;

    merge_value(ap, a);
}

static void add_name_to_pool(lily_ast_pool *ap, char *name)
{
    int oo_name_length = strlen(name);
    lily_ast_str_pool *str_pool = ap->oo_name_pool;
    int size_wanted = str_pool->pos + oo_name_length + 1;
    if (size_wanted > str_pool->size) {
        int new_size = str_pool->size;

        do {
            new_size *= 2;
        } while (size_wanted > new_size);

        char *new_str = lily_realloc(str_pool->str, new_size * sizeof(char));

        if (new_str == NULL)
            lily_raise_nomem(ap->raiser);

        str_pool->str = new_str;
        str_pool->size = new_size;
    }

    if (str_pool->pos == 0)
        strcpy(str_pool->str, name);
    else {
        str_pool->str[str_pool->pos] = '\0';
        strcat(str_pool->str + str_pool->pos, name);
    }

    str_pool->pos = size_wanted;
}

void lily_ast_push_oo_call(lily_ast_pool *ap, char *oo_name)
{
    int oo_index = ap->oo_name_pool->pos;
    add_name_to_pool(ap, oo_name);

    AST_ENTERABLE_INIT(a, tree_oo_call)
    a->oo_pool_index = oo_index;

    merge_value(ap, a);
}
