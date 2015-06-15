#include <string.h>

#include "lily_alloc.h"
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
    * Providing an api for freeze/thaw-ing state to allow for lambda processing.
      Again, the pool assumes that parser will use this correctly.
**/

/*  ACQUIRE_SPARE_TREE
    This macro creates and initializes an extra tree with the name given. */
#define ACQUIRE_SPARE_TREE(spare) \
lily_ast *spare; \
spare = ap->available_current; \
if (spare->next_tree == NULL) \
    add_new_tree(ap); \
\
ap->available_current = spare->next_tree; \
spare->next_arg = NULL; \
spare->line_num = *ap->lex_linenum; \
spare->parent = a;

/*  AST_COMMON_INIT
    This macro does common initialization for all new asts. This doesn't set
    result to NULL, because value trees set a result. */
#define AST_COMMON_INIT(a, tt) \
lily_ast *a; \
a = ap->available_current; \
if (a->next_tree == NULL) \
    add_new_tree(ap); \
\
ap->available_current = a->next_tree; \
a->tree_type = tt; \
a->next_arg = NULL; \
a->line_num = *ap->lex_linenum; \
a->parent = NULL;

/*  AST_ENTERABLE_INIT
    This macro does common initialization for trees that are meant to be
    entered (and will collect args). */
#define AST_ENTERABLE_INIT(a, tt) \
AST_COMMON_INIT(a, tt) \
a->args_collected = 0; \
a->arg_start = NULL; \
a->result = NULL;

static void add_save_entry(lily_ast_pool *);


/******************************************************************************/
/* Ast pool creation and teardown.                                            */
/******************************************************************************/

lily_ast_pool *lily_new_ast_pool(lily_options *options, int pool_size)
{
    lily_ast_pool *ap = lily_malloc(sizeof(lily_ast_pool));

    int i;
    lily_ast *last_tree;

    ap->active = NULL;
    ap->root = NULL;
    ap->save_chain = NULL;
    ap->available_start = NULL;
    ap->available_restore = NULL;
    ap->available_current = NULL;
    ap->freeze_chain = NULL;
    ap->membuf_start = 0;
    ap->save_depth = 0;
    ap->ast_membuf = NULL;

    last_tree = NULL;
    for (i = 0;i < pool_size;i++) {
        lily_ast *new_tree = lily_malloc(sizeof(lily_ast));

        new_tree->next_tree = last_tree;
        last_tree = new_tree;
    }

    ap->available_start = last_tree;
    ap->available_restore = last_tree;
    ap->available_current = last_tree;

    ap->ast_membuf = lily_membuf_new();
    add_save_entry(ap);

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

    /* Same idea as save entries: Could be at the beginning, middle, or end. */
    lily_ast_freeze_entry *freeze_iter = ap->freeze_chain;
    if (freeze_iter) {
        while (freeze_iter->prev)
            freeze_iter = freeze_iter->prev;

        lily_ast_freeze_entry *freeze_temp;
        while (freeze_iter) {
            freeze_temp = freeze_iter->next;

            lily_free(freeze_iter);

            freeze_iter = freeze_temp;
        }
    }

    if (ap->ast_membuf)
        lily_membuf_free(ap->ast_membuf);

    lily_free(ap);
}

/*  lily_ast_reset_pool
    Clear out old ast information for a new expression. */
void lily_ast_reset_pool(lily_ast_pool *ap)
{
    ap->root = NULL;
    ap->active = NULL;
    lily_membuf_restore_to(ap->ast_membuf, ap->membuf_start);
    ap->available_current = ap->available_restore;
}

/******************************************************************************/
/* Merging functions                                                          */
/******************************************************************************/

/*  merge_absorb
    new_tree wishes to absorb the current tree as an argument. */
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
    new_tree->args_collected = 1;
    new_tree->next_arg = NULL;
}

/*  merge_unary
    given is the top-most part of a unary expression. new_tree goes at the
    bottom of it. */
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
    Merge a new value against the current tree. This attempts to pick the right
    merge to used based upon the current tree. In most cases, this picks the
    correct tree. */
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
                /* Swallow the right side of the tree, then become it. */
                merge_absorb(ap, active->right, new_tree);
                active->right = new_tree;
                new_tree->parent = active;
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

/******************************************************************************/
/* Helper functions                                                           */
/******************************************************************************/

/*  add_save_entry
    Adds a new entry to the end of the pool's save entries. */
static void add_save_entry(lily_ast_pool *ap)
{
    lily_ast_save_entry *new_entry = lily_malloc(sizeof(lily_ast_save_entry));

    if (ap->save_chain == NULL) {
        ap->save_chain = new_entry;
        new_entry->prev = NULL;
    }
    else {
        ap->save_chain->next = new_entry;
        new_entry->prev = ap->save_chain;
    }

    new_entry->root_tree = NULL;
    new_entry->active_tree = NULL;
    new_entry->entered_tree = NULL;
    new_entry->next = NULL;
}

/*  add_new_tree
    Add a new tree to the linked list of currently available trees. */
static void add_new_tree(lily_ast_pool *ap)
{
    lily_ast *new_tree = lily_malloc(sizeof(lily_ast));

    new_tree->next_tree = NULL;

    ap->available_current->next_tree = new_tree;
}

/*  priority_for_op
    For binary: Determine the priority of the current op. */
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

static void push_typecast_type(lily_ast_pool *ap, lily_type *type)
{
    AST_COMMON_INIT(a, tree_type)
    a->typecast_type = type;

    merge_value(ap, a);
}

/*  push_tree_arg
    This adds an 'arg' as the newest argument to the given entered tree. */
static void push_tree_arg(lily_ast_pool *ap, lily_ast *entered_tree, lily_ast *arg)
{
    /* This happens when the parser sees () and calls to collect an argument
       just to be sure that anything in between is collected. It's fine, but
       there's also nothing to do here. */
    if (arg == NULL)
        return;

    if (entered_tree->arg_start == NULL)
        entered_tree->arg_start = arg;
    else {
        lily_ast *tree_iter = entered_tree->arg_start;
        while (tree_iter->next_arg != NULL)
            tree_iter = tree_iter->next_arg;

        tree_iter->next_arg = arg;
    }

    arg->parent = entered_tree;
    arg->next_arg = NULL;
    entered_tree->args_collected++;
}

/******************************************************************************/
/* Exported functions                                                         */
/******************************************************************************/

/*  lily_ast_collect_arg
    The current tree is an argument to whatever tree was saved last. Add it, then
    reset for another tree. */
void lily_ast_collect_arg(lily_ast_pool *ap)
{
    lily_ast_save_entry *entry = ap->save_chain->prev;

    push_tree_arg(ap, entry->entered_tree, ap->root);

    /* Keep all of the expressions independent. */
    ap->root = NULL;
    ap->active = NULL;
}

/*  lily_ast_enter_tree
    This begins a subexpression that takes comma-separated arguments. */
void lily_ast_enter_tree(lily_ast_pool *ap, lily_tree_type tree_type)
{
    AST_ENTERABLE_INIT(a, tree_type)

    merge_value(ap, a);

    /* Make it so ap->save_chain always points to a non-NULL entry, and that it
       is always an unused one. This allows fast, simple access. */
    lily_ast_save_entry *save_entry = ap->save_chain;
    if (save_entry->next == NULL)
        add_save_entry(ap);

    ap->save_chain = save_entry->next;
    save_entry->root_tree = ap->root;
    save_entry->active_tree = ap->active;
    save_entry->entered_tree = a;
    ap->save_depth++;

    ap->root = NULL;
    ap->active = NULL;
}

/*  lily_ast_leave_tree
    This leaves the currently-entered tree. Parser is expected to check that
    the right token was used to close the tree. The last tree is added as an
    argument, but no type-checking is done (emitter does that). */
void lily_ast_leave_tree(lily_ast_pool *ap)
{
    lily_ast_save_entry *entry = ap->save_chain->prev;

    push_tree_arg(ap, entry->entered_tree, ap->root);

    ap->root = entry->root_tree;
    ap->active = entry->active_tree;

    ap->save_chain = entry;
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
    return ap->save_chain->prev->entered_tree;
}

/*****************************************************************************/
/* Ast pushing functions                                                     */
/*****************************************************************************/

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

        active->parent = new_ast;

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

void lily_ast_enter_typecast(lily_ast_pool *ap, lily_type *type)
{
    lily_ast_enter_tree(ap, tree_typecast);
    push_typecast_type(ap, type);
    lily_ast_collect_arg(ap);
}

void lily_ast_push_unary_op(lily_ast_pool *ap, lily_expr_op op)
{
    AST_COMMON_INIT(a, tree_unary)
    a->left = NULL;
    a->priority = priority_for_op(op);
    a->op = op;

    merge_value(ap, a);
}

void lily_ast_push_local_var(lily_ast_pool *ap, lily_var *var)
{
    AST_COMMON_INIT(a, tree_local_var);
    a->result = (lily_sym *)var;
    a->sym = (lily_sym *)var;

    merge_value(ap, a);
}

void lily_ast_push_global_var(lily_ast_pool *ap, lily_var *var)
{
    AST_COMMON_INIT(a, tree_global_var);
    a->result = (lily_sym *)var;
    a->sym = (lily_sym *)var;

    merge_value(ap, a);
}

void lily_ast_push_upvalue(lily_ast_pool *ap, lily_var *var)
{
    AST_COMMON_INIT(a, tree_upvalue);
    a->sym = (lily_sym *)var;

    merge_value(ap, a);
}

void lily_ast_push_open_upvalue(lily_ast_pool *ap, lily_var *var)
{
    AST_COMMON_INIT(a, tree_open_upvalue);
    a->sym = (lily_sym *)var;

    merge_value(ap, a);
}

void lily_ast_push_defined_func(lily_ast_pool *ap, lily_var *func)
{
    AST_COMMON_INIT(a, tree_defined_func);
    a->result = (lily_sym *)func;
    a->sym = (lily_sym *)func;

    merge_value(ap, a);
}

void lily_ast_push_inherited_new(lily_ast_pool *ap, lily_var *func)
{
    AST_COMMON_INIT(a, tree_inherited_new);
    a->result = (lily_sym *)func;
    a->sym = (lily_sym *)func;

    merge_value(ap, a);
}

void lily_ast_push_literal(lily_ast_pool *ap, lily_tie *lit)
{
    AST_COMMON_INIT(a, tree_literal);
    a->result = (lily_sym *)lit;
    a->literal = lit;

    merge_value(ap, a);
}

void lily_ast_push_oo_access(lily_ast_pool *ap, char *oo_name)
{
    AST_ENTERABLE_INIT(a, tree_oo_access)
    a->membuf_pos = lily_membuf_add(ap->ast_membuf, oo_name);
    /* This MUST be set to NULL to clear out any prior value, as emitter checks
       it to make sure the tree is not double evaluated. */
    a->result = NULL;

    merge_value(ap, a);
}

void lily_ast_push_property(lily_ast_pool *ap, lily_prop_entry *prop)
{
    AST_COMMON_INIT(a, tree_property);
    a->property = prop;

    merge_value(ap, a);
}

void lily_ast_push_variant(lily_ast_pool *ap, lily_class *variant)
{
    AST_COMMON_INIT(a, tree_variant);
    a->variant_class = variant;

    merge_value(ap, a);
}

void lily_ast_push_self(lily_ast_pool *ap)
{
    AST_COMMON_INIT(a, tree_self);

    merge_value(ap, a);
}

void lily_ast_push_lambda(lily_ast_pool *ap, int start_line, char *lambda_text)
{
    AST_COMMON_INIT(a, tree_lambda)

    a->membuf_pos = lily_membuf_add(ap->ast_membuf, lambda_text);
    /* Without this next line, a multi-line lambda would start counting lines
       from where it stopped (resulting in invalid line numbers). */
    a->line_num = start_line;

    merge_value(ap, a);
}

void lily_ast_freeze_state(lily_ast_pool *ap)
{
    lily_ast_freeze_entry *new_entry;

    if (ap->freeze_chain == NULL ||
        (ap->freeze_chain->next == NULL &&
         ap->freeze_chain->in_use)) {
        new_entry = lily_malloc(sizeof(lily_ast_freeze_entry));

        if (ap->freeze_chain)
            ap->freeze_chain->next = new_entry;

        new_entry->prev = ap->freeze_chain;
        new_entry->next = NULL;
    }
    else if (ap->freeze_chain->in_use == 0)
        new_entry = ap->freeze_chain;
    else
        new_entry = ap->freeze_chain->next;

    new_entry->root = ap->root;
    new_entry->active = ap->active;
    new_entry->save_depth = ap->save_depth;
    new_entry->membuf_start = ap->membuf_start;
    new_entry->available_restore = ap->available_restore;
    new_entry->in_use = 1;

    ap->active = NULL;
    ap->root = NULL;
    ap->membuf_start = ap->ast_membuf->pos;
    ap->save_depth = 0;
    ap->available_restore = ap->available_current;

    ap->freeze_chain = new_entry;
}

void lily_ast_thaw_state(lily_ast_pool *ap)
{
    lily_ast_freeze_entry *entry = ap->freeze_chain;

    ap->root = entry->root;
    ap->active = entry->active;
    ap->membuf_start = entry->membuf_start;
    ap->save_depth = entry->save_depth;
    ap->available_restore = entry->available_restore;

    entry->in_use = 0;
    if (entry->prev)
        entry = entry->prev;
}
