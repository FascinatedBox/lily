#include <string.h>

#include "lily_ast.h"
#include "lily_debug.h"
#include "lily_emitter.h"
#include "lily_impl.h"
#include "lily_lexer.h"
#include "lily_parser.h"
#include "lily_msgbuf.h"
#include "lily_symtab.h"
#include "lily_vm.h"

/* These are flags for expression. */

/* Execute only one expression. */
#define EX_SINGLE     0x001
/* Get the lhs via expression_value. */
#define EX_NEED_VALUE 0x002
/* For if and elif to work, the expression has to be tested for being true or
   false. This tells the emitter to write in that test (o_jump_if_false) after
   writing the condition. This must be done within expression, because the
   ast is 'cleaned' by expression after each run. */
#define EX_CONDITION  0x004
/* Don't clear the ast within 'expression'. This allows the finished ast to be
   inspected. */
#define EX_SAVE_AST   0x010

/* These flags are for collect_args. */

/* Expect a name with every class given. Create a var for each class+name pair.
   This is suitable for collecting the args of a method. */
#define CA_MAKE_VARS  0x020

/* This is set if the variable is not inside another variable. This is suitable
   for collecting a method that may have named arguments. */
#define CA_TOPLEVEL   0x040

/* This is set if collect_args should only run once. If not set, then it will
   collect args separated by commas. */
#define CA_ONCE       0x100

#define NEED_NEXT_TOK(expected) \
lily_lexer(lex); \
if (lex->token != expected) \
    lily_raise(parser->raiser, lily_ErrSyntax, "Expected '%s', not %s.\n", \
               tokname(expected), tokname(lex->token)); \

#define NEED_CURRENT_TOK(expected) \
if (lex->token != expected) \
    lily_raise(parser->raiser, lily_ErrSyntax, "Expected '%s', not %s.\n", \
               tokname(expected), tokname(lex->token));

#define NEED_NEXT_TOK_MSG(expected, msg) \
lily_lexer(lex); \
if (lex->token != expected) \
    lily_raise(parser->raiser, lily_ErrSyntax, "Expected " msg ", not %s.\n", \
               tokname(lex->token)); \

/* table[token] = true/false. Used to check if a given token can be the start of
   an expression. */
static const int is_start_val[] = {
    0,
    0,
    0,
    0,
    1,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    1,
    1,
    1,
    1,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0
};

/* table[token] = binary_op. -1 indicates invalid. */
static const int bin_op_for_token[] = {
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    expr_assign,
    expr_eq_eq,
    expr_lt,
    expr_lt_eq,
    expr_gr,
    expr_gr_eq,
    -1,
    expr_not_eq,
    expr_plus,
    expr_minus,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    expr_logical_and,
    -1,
    expr_logical_or,
    -1,
    -1,
    -1
};

static void enter_oo_call(lily_parse_state *parser)
{
    lily_ast *active = parser->ast_pool->active;
    lily_class *cls;
    lily_var *call_var;

    while (active->tree_type == tree_parenth)
        active = active->arg_start;

    if (active->tree_type == tree_var)
        cls = active->result->sig->cls;
    else if (active->tree_type == tree_call)
        cls = active->result->sig->node.call->ret->cls;
    else if (active->tree_type == tree_binary) {
        lily_sig *sig = active->right->result->sig;

        /* If the rhs is a call, then this will use the return of that call.
           This can happen if the rhs was either a regular or oo call. The top
           call is all that needs to be checked, since it was the last. */
        if (active->right->tree_type == tree_call)
            cls = sig->node.call->ret->cls;
        else
            cls = sig->cls;
    }

    call_var = lily_find_class_callable(cls, parser->lex->label);
    if (call_var == NULL) {
        lily_raise(parser->raiser, lily_ErrSyntax,
                   "Class %s has no callable named %s.\n", cls->name,
                   parser->lex->label);
    }

    lily_ast_enter_subexpr(parser->ast_pool, tree_call, call_var);
}

static void do_unary_expr(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    while (1) {
        if (lex->token == tk_minus)
            lily_ast_push_unary_op(parser->ast_pool, expr_unary_minus);
        else if (lex->token == tk_not)
            lily_ast_push_unary_op(parser->ast_pool, expr_unary_not);
        else
            break;

        lily_lexer(lex);
    }
}

static void expression_value(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_symtab *symtab = parser->symtab;

    while (1) {
        if (lex->token == tk_word) {
            lily_var *var = lily_var_by_name(symtab, lex->label);

            if (var == NULL)
                lily_raise(parser->raiser, lily_ErrSyntax,
                           "Variable '%s' is undefined.\n", lex->label);

            lily_lexer(lex);
            if (lex->token == tk_left_parenth) {
                int cls_id = var->sig->cls->id;
                if (cls_id != SYM_CLASS_METHOD &&
                    cls_id != SYM_CLASS_FUNCTION)
                    lily_raise(parser->raiser, lily_ErrSyntax,
                               "%s is not callable.\n", var->name);

                /* New trees will get saved to the args section of this tree
                    when they are done. */
                lily_ast_enter_subexpr(parser->ast_pool, tree_call, var);

                lily_lexer(lex);
                if (lex->token == tk_right_parenth)
                    /* This call has no args (and therefore is not a value), so
                       let expression handle it. */
                    break;
                else
                    /* Get the first value of the call. */
                    continue;
            }
            else {
                lily_ast_push_sym(parser->ast_pool, (lily_sym *)var);
            }
        }
        else {
            lily_literal *lit;
            lily_class *cls;

            if (lex->token == tk_double_quote)
                cls = lily_class_by_id(symtab, SYM_CLASS_STR);
            else if (lex->token == tk_integer)
                cls = lily_class_by_id(symtab, SYM_CLASS_INTEGER);
            else if (lex->token == tk_number)
                cls = lily_class_by_id(symtab, SYM_CLASS_NUMBER);
            else if (lex->token == tk_minus || lex->token == tk_not) {
                do_unary_expr(parser);
                continue;
            }
            else if (lex->token == tk_left_parenth) {
                /* A parenth expression is essentially a call, but without the
                   var part. */
                lily_ast_enter_subexpr(parser->ast_pool, tree_parenth, NULL);
                lily_lexer(lex);
                continue;
            }
            else if (lex->token == tk_left_bracket) {
                lily_ast_enter_subexpr(parser->ast_pool, tree_list, NULL);
                lily_lexer(lex);
                continue;
            }
            else
                lily_raise(parser->raiser, lily_ErrSyntax,
                           "Expected a value, not '%s'.\n", 
                           tokname(lex->token));

            lit = lily_new_literal(symtab, cls, lex->value);
            lit->value = lex->value;

            lily_ast_push_sym(parser->ast_pool, (lily_sym *)lit);

            lily_lexer(lex);
        }
        if (lex->token == tk_dot) {
            NEED_NEXT_TOK(tk_word)
            enter_oo_call(parser);
            NEED_NEXT_TOK(tk_left_parenth);
            lily_lexer(lex);
            continue;
        }
        break;
    }
}

/* Expressions are divided into two states:
 * Value: A value is needed. ( is handled, because parenth expressions always
   yield a single value).
 * Op: The expression has enough values. Getting an op means another value will
   be necessary. If a word is found, it is the first word of the next
   expression (so no semicolons). ) is handled, because the expression is done
   and can properly be closed. */

static void expression(lily_parse_state *parser, int flags)
{
    lily_lex_state *lex = parser->lex;

    if ((flags & EX_NEED_VALUE) != 0)
        expression_value(parser);

    while (1) {
        while (1) {
            if (lex->token == tk_word) {
                if (parser->ast_pool->save_index != 0)
                    lily_raise(parser->raiser, lily_ErrSyntax,
                               "Expected ')' or a binary op, not a label.\n");

                break;
            }
            else if (lex->token == tk_right_parenth) {
                lily_ast_pop_tree(parser->ast_pool);

                lily_lexer(lex);
                if (parser->ast_pool->save_index == 0 &&
                    is_start_val[lex->token] == 1)
                    /* Since there are no parenths/calls left, then this value
                       must be the first in the next expression. */
                    break;
                else if (lex->token != tk_dot)
                    /* Since the parenth finished a value, the token should be
                       treated as a binary op. */
                    continue;
                else {
                    /* 'a.concat("b").concat("c")'. Do a normal oo merge. */
                    NEED_NEXT_TOK(tk_word)
                    enter_oo_call(parser);
                    NEED_NEXT_TOK(tk_left_parenth);
                    lily_lexer(lex);
                }
            }
            else if (lex->token == tk_right_bracket) {
                lily_ast_pop_tree(parser->ast_pool);
                lily_lexer(lex);
                if (parser->ast_pool->save_index == 0 &&
                    is_start_val[lex->token] == 1) {
                    /* Since there are no parenths/calls left, then this value
                       must be the first in the next expression. */
                    break;
                }
                else
                    continue;
            }
            else if (lex->token == tk_comma) {
                if (parser->ast_pool->active == NULL)
                    lily_raise(parser->raiser, lily_ErrSyntax,
                               "Expected a value, not ','.\n");

                /* If this is inside of a decl list (integer a, b, c...), then
                   the comma is the end of the decl unless it's part of a call
                   used in the decl (integer a = add(1, 2), b = 1...). */
                if (((flags & EX_NEED_VALUE) == 0) &&
                    parser->ast_pool->save_index == 0) {
                    break;
                }
                lily_ast_collect_arg(parser->ast_pool);
                lily_lexer(lex);
            }
            else {
                int expr_op = bin_op_for_token[lex->token];
                if (expr_op != -1) {
                    lily_ast_push_binary_op(parser->ast_pool,
                            (lily_expr_op)expr_op);
                    lily_lexer(lex);
                }
                else if (lex->token == tk_colon ||
                         lex->token == tk_right_curly ||
                         lex->token == tk_end_tag || lex->token == tk_eof)
                    break;
                else {
                    lily_raise(parser->raiser, lily_ErrSyntax,
                               "Expected maybe a binary operator, not %s.\n",
                               tokname(lex->token));
                }
            }
            expression_value(parser);
        }
        if (flags & EX_SINGLE) {
            if (flags & EX_CONDITION) {
                lily_emit_conditional(parser->emit, parser->ast_pool->root);
                lily_ast_reset_pool(parser->ast_pool);
            }
            else if (!(flags & EX_SAVE_AST)) {
                lily_emit_ast(parser->emit, parser->ast_pool->root);
                lily_ast_reset_pool(parser->ast_pool);
            }
        }
        break;
    }
}

static void declaration(lily_parse_state *parser, lily_class *cls)
{
    lily_lex_state *lex = parser->lex;
    lily_var *var;

    while (1) {
        /* This starts at the class name, or the comma. The label is next. */
        NEED_NEXT_TOK_MSG(tk_word, "a variable name")

        var = lily_var_by_name(parser->symtab, lex->label);
        if (var != NULL)
            lily_raise(parser->raiser, lily_ErrSyntax,
                       "%s has already been declared.\n", var->name);

        var = lily_try_new_var(parser->symtab, cls, lex->label);
        if (var == NULL)
            lily_raise_nomem(parser->raiser);

        lily_lexer(parser->lex);
        /* Handle an initializing assignment, if there is one. */
        if (lex->token == tk_equal) {
            lily_ast_push_sym(parser->ast_pool, (lily_sym *)var);
            expression(parser, EX_SINGLE);
        }

        /* This is the start of the next statement. */
        if (lex->token == tk_word || lex->token == tk_end_tag ||
            lex->token == tk_eof)
            break;
        else if (lex->token != tk_comma) {
            lily_raise(parser->raiser, lily_ErrSyntax,
                       "Expected ',' or ')', not %s.\n", tokname(lex->token));
        }
        /* else comma, so just jump back up. */
    }
}

static void handle_return(lily_parse_state *parser)
{
    lily_sig *ret_sig = parser->emit->target_ret;
    if (ret_sig != NULL) {
        expression(parser, EX_NEED_VALUE | EX_SINGLE | EX_SAVE_AST);
        lily_emit_return(parser->emit, parser->ast_pool->root, ret_sig);
        lily_ast_reset_pool(parser->ast_pool);
    }
    else
        lily_emit_return_noval(parser->emit);
}

static void parse_simple_condition(lily_parse_state *parser)
{
    /* In a simple condition, each if, elif, and else have only a single
       expression. This is called when an 'if' is caught, and the token after
       the : is not {. */
    int key_id;
    lily_lex_state *lex = parser->lex;

    while (1) {
        if (lex->token == tk_word)
            key_id = lily_keyword_by_name(parser->lex->label);
        else
            key_id = -1;

        if (key_id == KEY_RETURN) {
            /* Skip the 'return' keyword. */
            lily_lexer(lex);
            handle_return(parser);
        }
        else
            expression(parser, EX_NEED_VALUE | EX_SINGLE);

        if (lex->token == tk_word) {
            key_id = lily_keyword_by_name(parser->lex->label);
            if (key_id == KEY_IF) {
                /* Close this branch and start another one. */
                lily_emit_pop_block(parser->emit);
                lily_lexer(lex);
                lily_emit_push_block(parser->emit, BLOCK_IF);
                expression(parser, EX_NEED_VALUE | EX_SINGLE |
                               EX_CONDITION);

                if (lex->token != tk_colon)
                    lily_raise(parser->raiser, lily_ErrSyntax,
                               "Expected ':', not %s.\n", tokname(lex->token));

                lily_lexer(lex);
                if (lex->token == tk_left_curly) {
                    /* This func handles single-line ifs, but this is a
                       multi-line one. Swallow the {, then yield control. */
                    lily_lexer(lex);
                    return;
                }
            }
            else if (key_id == KEY_ELIF) {
                lily_emit_clear_block(parser->emit, /*have_else=*/0);
                lily_lexer(lex);
                expression(parser, EX_NEED_VALUE | EX_SINGLE |
                               EX_CONDITION);

                if (lex->token != tk_colon)
                    lily_raise(parser->raiser, lily_ErrSyntax,
                               "Expected ':', not %s.\n", tokname(lex->token));

                lily_lexer(lex);
            }
            else if (key_id == KEY_ELSE) {
                lily_emit_clear_block(parser->emit, /*have_else=*/1);

                lily_lexer(lex);
                if (lex->token != tk_colon)
                    lily_raise(parser->raiser, lily_ErrSyntax,
                               "Expected ':', not %s.\n", tokname(lex->token));

                lily_lexer(lex);
            }
            else
                break;
        }
        else
            break;
    }
    lily_emit_pop_block(parser->emit);
}

static void collect_args(lily_parse_state *parser, int *count, int flags)
{
    lily_lex_state *lex = parser->lex;
    lily_class *cls;
    int collect_multi, i;

    /* This is inverted to collect_multi because most collections will be of
       multiple args. */
    collect_multi = !(flags & CA_ONCE);
    i = 0;

    while (1) {
        lily_var *var;
        NEED_CURRENT_TOK(tk_word)
        cls = lily_class_by_name(parser->symtab, lex->label);
        if (cls == NULL)
            lily_raise(parser->raiser, lily_ErrSyntax,
                       "unknown class name %s.\n", lex->label);

        if (flags & CA_MAKE_VARS) {
            NEED_NEXT_TOK(tk_word)
            var = lily_try_new_var(parser->symtab, cls, lex->label);
            if (var == NULL)
                lily_raise_nomem(parser->raiser);
        }

        /* todo: Support functions later. */
        if (cls->id == SYM_CLASS_METHOD) {
            int collect_flags, save_pos, j;
            lily_class *call_ret_class;
            lily_call_sig *call_sig;

            collect_flags = 0;
            if (flags & CA_TOPLEVEL)
                collect_flags |= CA_MAKE_VARS;

            call_sig = var->sig->node.call;
            save_pos = parser->sig_stack_pos;
            NEED_NEXT_TOK(tk_left_parenth)
            lily_lexer(lex);

            if (lex->token != tk_right_parenth) {
                collect_args(parser, &j, collect_flags);
                NEED_CURRENT_TOK(tk_right_parenth)
            }
            else
                j = 0;

            NEED_NEXT_TOK(tk_colon)
            NEED_NEXT_TOK(tk_word)
            call_ret_class = lily_class_by_name(parser->symtab, lex->label);
            if (call_ret_class == NULL) {
                if (strcmp(lex->label, "nil") != 0)
                    lily_raise(parser->raiser, lily_ErrSyntax,
                               "unknown class name %s.\n", lex->label);
            }
            else
                call_sig->ret = call_ret_class->sig;

            /* If the call has args, pull them from the sig stack. */
            if (j != 0) {
                int k;
                lily_sig **args = lily_malloc(sizeof(lily_sig *) * j);
                if (args == NULL)
                    lily_raise_nomem(parser->raiser);

                if (flags & CA_TOPLEVEL) {
                    lily_method_val *mval = var->value.method;
                    lily_var *iter_var = var->next;
                    k = 0;
                    mval->first_arg = iter_var;
                    mval->last_arg = parser->symtab->var_top;
                    while (iter_var) {
                        args[k] = iter_var->sig;
                        k++;
                        iter_var = iter_var->next;
                    }
                }
                else {
                    for (k = 0;k < j;k++, save_pos++)
                        args[k] = parser->sig_stack[save_pos];
                    parser->sig_stack_pos -= j;
                }
                call_sig->args = args;
                call_sig->num_args = j;
            }
        }

        if ((flags & CA_MAKE_VARS) == 0) {
            if (parser->sig_stack_pos == parser->sig_stack_size) {
                parser->sig_stack_size *= 2;
                lily_sig **new_sig_stack = lily_realloc(parser->sig_stack,
                    sizeof(lily_sig *) * parser->sig_stack_size);

                if (new_sig_stack == NULL)
                    lily_raise_nomem(parser->raiser);

                parser->sig_stack = new_sig_stack;
            }
            parser->sig_stack[parser->sig_stack_pos] = cls->sig;
            parser->sig_stack_pos++;
        }
        i++;

        lily_lexer(lex);
        if (lex->token == tk_comma && collect_multi)
            lily_lexer(lex);
        else {
            *count = i;
            break;
        }
    }
}

static void statement(lily_parse_state *parser)
{
    char *label = parser->lex->label;
    int key_id;
    lily_class *lclass;

    key_id = lily_keyword_by_name(label);
    if (key_id != -1) {
        lily_lex_state *lex = parser->lex;
        lily_lexer(lex);

        if (key_id == KEY_RETURN)
            handle_return(parser);
        else {
            if (key_id == KEY_IF) {
                lily_emit_push_block(parser->emit, BLOCK_IF);
                expression(parser, EX_NEED_VALUE | EX_SINGLE | EX_CONDITION);
            }
            else if (key_id == KEY_ELIF) {
                lily_emit_clear_block(parser->emit, /*have_else=*/0);
                expression(parser, EX_NEED_VALUE | EX_SINGLE | EX_CONDITION);
            }
            else if (key_id == KEY_ELSE)
                lily_emit_clear_block(parser->emit, /*have_else=*/1);

            NEED_CURRENT_TOK(tk_colon)

            lily_lexer(lex);
            if (key_id == KEY_IF) {
                if (lex->token != tk_left_curly)
                    parse_simple_condition(parser);
                else {
                    NEED_CURRENT_TOK(tk_left_curly)

                    lily_lexer(parser->lex);
                }
            }
        }
    }
    else {
        lclass = lily_class_by_name(parser->symtab, label);

        if (lclass != NULL) {
            if (lclass->id == SYM_CLASS_METHOD) {
                lily_var *save_var = parser->symtab->var_top;
                int j;
                lily_lex_state *lex = parser->lex;

                collect_args(parser, &j, CA_TOPLEVEL | CA_MAKE_VARS);
                NEED_CURRENT_TOK(tk_left_curly)
                lily_emit_enter_method(parser->emit, save_var->next);
                lily_lexer(lex);
            }
            else if (lclass->id == SYM_CLASS_LIST) {
                lily_lex_state *lex = parser->lex;
                int j;
                lily_var *list_var;
                lily_sig *elem_sig;

                NEED_NEXT_TOK(tk_left_bracket)
                lily_lexer(lex);
                collect_args(parser, &j, CA_ONCE);
                NEED_CURRENT_TOK(tk_right_bracket)
                NEED_NEXT_TOK(tk_word)
                list_var = lily_try_new_var(parser->symtab, lclass, lex->label);
                if (list_var == NULL)
                    lily_raise_nomem(parser->raiser);

                elem_sig = parser->sig_stack[parser->sig_stack_pos-1];
                parser->sig_stack_pos--;
                list_var->sig->node.value_sig = elem_sig;
            }
            else
                declaration(parser, lclass);
        }
        else
            expression(parser, EX_NEED_VALUE | EX_SINGLE);
    }
}

void lily_free_parse_state(lily_parse_state *parser)
{
    lily_ast_free_pool(parser->ast_pool);
    lily_free_symtab(parser->symtab);
    lily_free_lex_state(parser->lex);
    lily_free_emit_state(parser->emit);
    lily_free_vm_state(parser->vm);
    lily_free(parser->sig_stack);
    lily_free(parser->raiser->message);
    lily_free(parser->raiser);
    lily_free(parser);
}

lily_parse_state *lily_new_parse_state(void)
{
    lily_parse_state *s = lily_malloc(sizeof(lily_parse_state));
    lily_raiser *raiser = lily_malloc(sizeof(lily_raiser));
    lily_sig **sig_stack = lily_malloc(4 * sizeof(lily_sig *));

    if (s == NULL || raiser == NULL || sig_stack == NULL) {
        lily_free(sig_stack);
        lily_free(raiser);
        lily_free(s);
        return NULL;
    }

    s->sig_stack = sig_stack;
    s->sig_stack_pos = 0;
    s->sig_stack_size = 4;
    raiser->line_adjust = 0;
    raiser->message = NULL;
    s->ast_pool = lily_ast_init_pool(raiser, 8);
    s->raiser = raiser;

    s->symtab = lily_new_symtab(raiser);
    s->emit = lily_new_emit_state(raiser);
    s->lex = lily_new_lex_state(raiser);
    s->vm = lily_new_vm_state(raiser);

    if (s->lex == NULL || s->emit == NULL || s->symtab == NULL ||
        s->ast_pool == NULL || s->vm == NULL) {
        if (s->symtab != NULL)
            lily_free_symtab(s->symtab);
        if (s->emit != NULL)
            lily_free_emit_state(s->emit);
        if (s->lex != NULL)
            lily_free_lex_state(s->lex);
        if (s->ast_pool != NULL)
            lily_ast_free_pool(s->ast_pool);
        if (s->vm != NULL)
            lily_free_vm_state(s->vm);
        lily_free(sig_stack);
        lily_free(raiser);
        lily_free(s);
        return NULL;
    }

    s->vm->main = s->symtab->var_start;
    s->symtab->lex_linenum = &s->lex->line_num;
    s->ast_pool->lex_linenum = &s->lex->line_num;
    s->emit->symtab = s->symtab;
    lily_emit_enter_method(s->emit, s->symtab->var_start);
    return s;
}

void lily_parser(lily_parse_state *parser)
{
    /* Must do this first, in the rare case this next call fails. */
    parser->mode = pm_parse;

    lily_lex_state *lex = parser->lex;
    lily_lexer(lex);

    while (1) {
        if (lex->token == tk_word)
            statement(parser);
        else if (lex->token == tk_right_curly) {
            lily_emit_pop_block(parser->emit);
            lily_lexer(parser->lex);
        }
        else if (lex->token == tk_end_tag || lex->token == tk_eof) {
            lily_emit_vm_return(parser->emit);
            /* Show symtab until the bugs are gone. */
            lily_show_symtab(parser->symtab);

            parser->mode = pm_execute;
            lily_vm_execute(parser->vm);
            parser->mode = pm_parse;

            /* Clear @main for the next pass. */
            lily_reset_main(parser->emit);

            if (lex->token == tk_end_tag) {
                lily_lexer_handle_page_data(parser->lex);
                if (lex->token == tk_eof)
                    break;
            }
            else
                break;
        }
        else
            lily_raise(parser->raiser, lily_ErrSyntax, "Unexpected token %s.\n",
                       tokname(lex->token));
    }
}

/* Public parse-starting routines. */

int lily_parse_file(lily_parse_state *parser, char *filename)
{
    if (setjmp(parser->raiser->jump) == 0) {
        lily_load_file(parser->lex, filename);
        lily_parser(parser);
        return 1;
    }

    return 0;
}

int lily_parse_string(lily_parse_state *parser, char *str)
{
    if (setjmp(parser->raiser->jump) == 0) {
        lily_load_str(parser->lex, str);
        lily_parser(parser);
        parser->lex->lex_buffer = NULL;
        return 1;
    }

    return 0;
}
