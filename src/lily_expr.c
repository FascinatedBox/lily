#include <string.h>

#include "lily_expr.h"
#include "lily_alloc.h"

#define AST_COMMON_INIT(a, tt) \
lily_ast *a; \
a = es->next_available; \
if (a->next_tree == NULL) \
    add_new_tree(es); \
\
es->next_available = a->next_tree; \
a->tree_type = tt; \
a->next_arg = NULL; \
a->line_num = *es->lex_linenum; \
a->maybe_result_pos = 0; \
a->parent = NULL;

#define AST_ENTERABLE_INIT(a, tt) \
AST_COMMON_INIT(a, tt) \
a->args_collected = 0; \
a->arg_start = NULL; \
a->result = NULL;

static void add_save_entry(lily_expr_state *);

/***
 *      ____       _
 *     / ___|  ___| |_ _   _ _ __
 *     \___ \ / _ \ __| | | | '_ \
 *      ___) |  __/ |_| |_| | |_) |
 *     |____/ \___|\__|\__,_| .__/
 *                          |_|
 */

lily_expr_state *lily_new_expr_state(void)
{
    lily_expr_state *es = lily_malloc(sizeof(*es));

    int i;
    lily_ast *last_tree = NULL;
    for (i = 0;i < 4;i++) {
        lily_ast *new_tree = lily_malloc(sizeof(*new_tree));

        new_tree->next_tree = last_tree;
        last_tree = new_tree;
    }

    es->first_tree = last_tree;
    es->next_available = last_tree;
    es->save_chain = NULL;
    es->save_depth = 0;
    es->pile_start = 0;
    es->pile_current = 0;
    es->prev = NULL;
    es->root = NULL;
    es->active = NULL;

    add_save_entry(es);

    return es;
}

void lily_free_expr_state(lily_expr_state *es)
{
    lily_ast *ast_iter = es->first_tree;
    lily_ast *ast_temp;

    while (ast_iter) {
        ast_temp = ast_iter->next_tree;
        lily_free(ast_iter);
        ast_iter = ast_temp;
    }

    lily_ast_save_entry *save_iter = es->save_chain;
    lily_ast_save_entry *save_temp;
    while (save_iter) {
        save_temp = save_iter->next;
        lily_free(save_iter);
        save_iter = save_temp;
    }

    lily_free(es);
}

static void add_save_entry(lily_expr_state *es)
{
    lily_ast_save_entry *new_entry = lily_malloc(sizeof(*new_entry));

    if (es->save_chain == NULL) {
        es->save_chain = new_entry;
        new_entry->prev = NULL;
    }
    else {
        es->save_chain->next = new_entry;
        new_entry->prev = es->save_chain;
    }

    new_entry->root_tree = NULL;
    new_entry->active_tree = NULL;
    new_entry->entered_tree = NULL;
    new_entry->next = NULL;
}

static void add_new_tree(lily_expr_state *es)
{
    lily_ast *new_tree = lily_malloc(sizeof(*new_tree));

    new_tree->next_tree = NULL;

    es->next_available->next_tree = new_tree;
}

/***
 *      __  __
 *     |  \/  | ___ _ __ __ _  ___
 *     | |\/| |/ _ \ '__/ _` |/ _ \
 *     | |  | |  __/ | | (_| |  __/
 *     |_|  |_|\___|_|  \__, |\___|
 *                      |___/
 */

/** The expression state is responsible for managing the root, the current, and
    the saved trees that are within it. A central requirement is that parser is
    giving the right directions.
    Merging is done very bluntly:
    * Binary trees are merged so that the lowest priority leans toward becoming
      the root while the highest tends to go down.
    * Unary doesn't say that it's current, but will dig down and push a value
      against the highest binary tree. This is to keep proper precedence of
      operations like !x.somecall() so that the ! goes after the call.
    * Everything else goes through absorbing merge, wherein the newest thing
      becomes an argument and a child of the oldest thing. */

static void merge_absorb(lily_expr_state *es, lily_ast *given, lily_ast *new_tree)
{
    /* If the swallowed tree is active/root, become those. */
    if (given == es->active) {
        es->active = new_tree;
        if (given == es->root)
            es->root = new_tree;
    }

    given->parent = new_tree;
    new_tree->arg_start = given;
    new_tree->args_collected = 1;
    new_tree->next_arg = NULL;
}

static void merge_unary(lily_expr_state *es, lily_ast *given, lily_ast *new_tree)
{
    /* Unary ops are right to left, so newer trees go lower. Descend to find
       the lowest unary tree, so that the merge will be against the value it
       holds. */
    while (given->tree_type == tree_unary &&
           given->left != NULL &&
           given->left->tree_type == tree_unary)
        given = given->left;

    if (given->left == NULL)
        /* Either a value, or another unary tree. */
        given->left = new_tree;
    else {
        /* This won't be a unary, binary, or a value, so absorb the unary's
           value and become the new unary value. */
        merge_absorb(es, given->left, new_tree);
        given->left = new_tree;
    }

    new_tree->parent = given;
}

static void merge_value(lily_expr_state *es, lily_ast *new_tree)
{
    lily_ast *active = es->active;

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
                merge_unary(es, active->right, new_tree);
            else {
                /* Swallow the right side of the tree, then become it. */
                merge_absorb(es, active->right, new_tree);
                active->right = new_tree;
                new_tree->parent = active;
            }
        }
        else if (active->tree_type == tree_unary)
            merge_unary(es, active, new_tree);
        else
            merge_absorb(es, active, new_tree);
    }
    else {
        /* If there's no active, then there's no root. Become both. */
        es->root = new_tree;
        es->active = new_tree;
    }
}

/***
 *      __  __ _
 *     |  \/  (_)___  ___
 *     | |\/| | / __|/ __|
 *     | |  | | \__ \ (__
 *     |_|  |_|_|___/\___|
 *
 */

static uint8_t priority_for_op(lily_expr_op o)
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
        /* Put pipes lower so they scoop up as much as possible. This seems
           right. */
        case expr_func_pipe:
            prio = 3;
            break;
        case expr_eq_eq:
        case expr_not_eq:
        case expr_lt:
        case expr_gr:
        case expr_lt_eq:
        case expr_gr_eq:
            prio = 4;
            break;
        case expr_bitwise_or:
        case expr_bitwise_xor:
        case expr_bitwise_and:
            prio = 5;
            break;
        case expr_left_shift:
        case expr_right_shift:
            prio = 6;
            break;
        case expr_plus:
        case expr_minus:
            prio = 7;
            break;
        case expr_multiply:
        case expr_divide:
        case expr_modulo:
            prio = 8;
            break;
        default:
            /* Won't happen, but makes -Wall happy. */
            prio = -1;
            break;
    }

    return prio;
}

static void push_tree_arg(lily_expr_state *es, lily_ast *entered_tree,
        lily_ast *arg)
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

void lily_es_collect_arg(lily_expr_state *es)
{
    lily_ast_save_entry *entry = es->save_chain;

    push_tree_arg(es, entry->entered_tree, es->root);

    /* Keep all of the expressions independent. */
    es->root = NULL;
    es->active = NULL;
}

void lily_es_enter_tree(lily_expr_state *es, lily_tree_type tree_type)
{
    AST_ENTERABLE_INIT(a, tree_type)

    merge_value(es, a);

    lily_ast_save_entry *save_entry = es->save_chain;
    /* The entered tree is only NULL if it's not being used. */
    if (save_entry->entered_tree != NULL) {
        if (save_entry->next == NULL)
            add_save_entry(es);

        es->save_chain = es->save_chain->next;
        save_entry = es->save_chain;
    }

    save_entry->root_tree = es->root;
    save_entry->active_tree = es->active;
    save_entry->entered_tree = a;
    es->save_depth++;

    es->root = NULL;
    es->active = NULL;
}

void lily_es_leave_tree(lily_expr_state *es)
{
    lily_ast_save_entry *entry = es->save_chain;

    push_tree_arg(es, entry->entered_tree, es->root);

    es->root = entry->root_tree;
    es->active = entry->active_tree;

    if (es->save_chain->prev == NULL)
        /* The NULL is a signal it's no longer being used. */
        es->save_chain->entered_tree = NULL;
    else
        es->save_chain = es->save_chain->prev;

    es->save_depth--;
}

/* Get the tree that was entered last. */
lily_ast *lily_es_get_saved_tree(lily_expr_state *es)
{
    return es->save_chain->entered_tree;
}

/***
 *      ____            _
 *     |  _ \ _   _ ___| |__   ___ _ __ ___
 *     | |_) | | | / __| '_ \ / _ \ '__/ __|
 *     |  __/| |_| \__ \ | | |  __/ |  \__ \
 *     |_|    \__,_|___/_| |_|\___|_|  |___/
 *
 */

/** These functions are responsible for pushing and merging some new value into
    the expression state. These are in need of cleaning up, as there are far too
    many of them compared to how many values that there are.
    But at their core, most are simple and distinct: Create a tree holding some
    value, and push the value. */

void lily_es_push_binary_op(lily_expr_state *es, lily_expr_op op)
{
    /* Call it 'new_ast', because this merge is a bit complex. */
    AST_COMMON_INIT(new_ast, tree_binary)
    new_ast->priority = priority_for_op(op);
    new_ast->op = op;
    new_ast->left = NULL;
    new_ast->right = NULL;

    lily_ast *active = es->active;

    /* Active is always non-NULL, because binary always comes after a value of
       some kind. */
    if (active->tree_type < tree_binary) {
        /* Only a value or call so far. The binary op takes over. */
        if (es->root == active)
            es->root = new_ast;

        active->parent = new_ast;

        new_ast->left = active;
        es->active = new_ast;
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
            es->active = new_ast;
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
                es->root = new_ast;
            }
            new_ast->left = tree;
            es->active = new_ast;
        }
    }
}

static void push_type(lily_expr_state *es, lily_type *type)
{
    AST_COMMON_INIT(a, tree_type)
    a->type = type;

    merge_value(es, a);
}

void lily_es_enter_typecast(lily_expr_state *es, lily_type *type)
{
    lily_es_enter_tree(es, tree_typecast);
    push_type(es, type);
    lily_es_collect_arg(es);
}

void lily_es_push_unary_op(lily_expr_state *es, lily_expr_op op)
{
    AST_COMMON_INIT(a, tree_unary)

    a->left = NULL;
    a->op = op;

    merge_value(es, a);
}

void lily_es_push_local_var(lily_expr_state *es, lily_var *var)
{
    AST_COMMON_INIT(a, tree_local_var);
    a->result = (lily_sym *)var;
    a->sym = (lily_sym *)var;

    merge_value(es, a);
}

void lily_es_push_global_var(lily_expr_state *es, lily_var *var)
{
    AST_COMMON_INIT(a, tree_global_var);
    a->result = (lily_sym *)var;
    a->sym = (lily_sym *)var;

    merge_value(es, a);
}

void lily_es_push_upvalue(lily_expr_state *es, lily_var *var)
{
    AST_COMMON_INIT(a, tree_upvalue);
    a->sym = (lily_sym *)var;

    merge_value(es, a);
}

void lily_es_push_defined_func(lily_expr_state *es, lily_var *func)
{
    AST_COMMON_INIT(a, tree_defined_func);
    a->result = (lily_sym *)func;
    a->sym = (lily_sym *)func;

    merge_value(es, a);
}

void lily_es_push_method(lily_expr_state *es, lily_var *func)
{
    AST_COMMON_INIT(a, tree_method);
    a->result = (lily_sym *)func;
    a->sym = (lily_sym *)func;

    merge_value(es, a);
}

void lily_es_push_static_func(lily_expr_state *es, lily_var *func)
{
    AST_COMMON_INIT(a, tree_static_func);
    a->result = (lily_sym *)func;
    a->sym = (lily_sym *)func;

    merge_value(es, a);
}

void lily_es_push_inherited_new(lily_expr_state *es, lily_var *func)
{
    AST_COMMON_INIT(a, tree_inherited_new);
    a->result = (lily_sym *)func;
    a->sym = (lily_sym *)func;

    merge_value(es, a);
}

void lily_es_push_literal(lily_expr_state *es, lily_type *t, uint16_t reg_spot)
{
    AST_COMMON_INIT(a, tree_literal);
    a->type = t;
    a->literal_reg_spot = reg_spot;

    merge_value(es, a);
}

void lily_es_push_boolean(lily_expr_state *es, int16_t value)
{
    AST_COMMON_INIT(a, tree_boolean);
    a->backing_value = value;

    merge_value(es, a);
}

void lily_es_push_byte(lily_expr_state *es, uint8_t value)
{
    AST_COMMON_INIT(a, tree_byte);
    a->backing_value = value;

    merge_value(es, a);
}

void lily_es_push_integer(lily_expr_state *es, int16_t value)
{
    AST_COMMON_INIT(a, tree_integer);
    a->backing_value = value;

    merge_value(es, a);
}

void lily_es_push_property(lily_expr_state *es, lily_prop_entry *prop)
{
    AST_COMMON_INIT(a, tree_property);
    a->property = prop;

    merge_value(es, a);
}

void lily_es_push_variant(lily_expr_state *es, lily_variant_class *variant)
{
    AST_COMMON_INIT(a, tree_variant);
    a->variant = variant;

    merge_value(es, a);
}

void lily_es_push_self(lily_expr_state *es)
{
    AST_COMMON_INIT(a, tree_self);

    merge_value(es, a);
}

void lily_es_push_text(lily_expr_state *es, lily_tree_type tt, uint32_t start,
        int pos)
{
    AST_COMMON_INIT(a, tt)

    a->pile_pos = pos;
    a->line_num = start;

    merge_value(es, a);
}
