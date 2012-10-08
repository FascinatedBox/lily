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
#define EX_SINGLE     0x01
/* Get the lhs via expression_value. */
#define EX_NEED_VALUE 0x02
/* For if and elif to work, the expression has to be tested for being true or
   false. This tells the emitter to write in that test (o_jump_if_false) after
   writing the condition. This must be done within expression, because the
   ast is 'cleaned' by expression after each run. */
#define EX_CONDITION  0x04
/* Don't clear the ast within 'expression'. This allows the finished ast to be
   inspected. */
#define EX_SAVE_AST   0x10

#define NEED_NEXT_TOK(expected) \
lily_lexer(lex); \
if (lex->token != expected) \
    lily_raise(parser->error, "Expected '%s', not %s.\n", \
               tokname(expected), tokname(lex->token)); \

#define NEED_CURRENT_TOK(expected) \
if (lex->token != expected) \
    lily_raise(parser->error, "Expected '%s', not %s.\n", \
               tokname(expected), tokname(lex->token));

#define NEED_NEXT_TOK_MSG(expected, msg) \
lily_lexer(lex); \
if (lex->token != expected) \
    lily_raise(parser->error, "Expected " msg ", not %s.\n", \
               tokname(lex->token)); \

static void enter_oo_call(lily_parse_state *parser)
{
    lily_ast *active = parser->ast_pool->active;
    lily_class *cls;
    lily_var *call_var;

    if (active->expr_type == var)
        cls = active->result->sig->cls;
    else if (active->expr_type == call)
        cls = active->result->sig->node.call->ret->cls;
    else if (active->expr_type == binary) {
        lily_sig *sig = active->right->result->sig;

        /* If the rhs is a call, then this will use the return of that call.
           This can happen if the rhs was either a regular or oo call. The top
           call is all that needs to be checked, since it was the last. */
        if (active->right->expr_type == call)
            cls = sig->node.call->ret->cls;
        else
            cls = sig->cls;
    }

    call_var = lily_find_class_callable(cls, parser->lex->label);
    if (call_var == NULL) {
        lily_raise(parser->error, "Class %s has no callable named %s.\n",
            cls->name, parser->lex->label);
    }

    lily_ast_enter_call(parser->ast_pool, call_var);
}

static void expression_value(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_symtab *symtab = parser->symtab;

    while (1) {
        if (lex->token == tk_word) {
            lily_var *var = lily_var_by_name(symtab, lex->label);

            if (var == NULL)
                lily_raise(parser->error, "Variable '%s' is undefined.\n",
                           lex->label);

            if (var->sig->cls->id == SYM_CLASS_FUNCTION ||
                var->sig->cls->id == SYM_CLASS_METHOD) {
                /* New trees will get saved to the args section of this tree
                    when they are done. */
                lily_ast_enter_call(parser->ast_pool, var);

                NEED_NEXT_TOK(tk_left_parenth)

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

                lily_lexer(lex);
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
            else {
                lily_raise(parser->error, "Expected a value, not '%s'.\n",
                           tokname(lex->token));
            }

            lit = lily_new_literal(symtab, cls);
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
                    lily_raise(parser->error,
                            "Expected ')' or a binary op, not a label.\n");

                break;
            }
            else if (lex->token == tk_right_parenth) {
                lily_ast_pop_tree(parser->ast_pool);

                lily_lexer(lex);
                /* If no functions, and a word, then the word is the start of
                   the next statement. */
                if (lex->token == tk_word && parser->ast_pool->save_index == 0)
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
            else if (lex->token == tk_comma) {
                if (parser->ast_pool->active == NULL)
                    lily_raise(parser->error, "Expected a value, not ','.\n");

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
                lily_expr_op op;
                if (lex->token == tk_equal)
                    op = expr_assign;
                else if (lex->token == tk_plus)
                    op = expr_plus;
                else if (lex->token == tk_minus)
                    op = expr_minus;
                else if (lex->token == tk_eq_eq)
                    op = expr_eq_eq;
                else if (lex->token == tk_lt)
                    op = expr_lt;
                else if (lex->token == tk_lt_eq)
                    op = expr_lt_eq;
                else if (lex->token == tk_gr)
                    op = expr_gr;
                else if (lex->token == tk_gr_eq)
                    op = expr_gr_eq;
                else if (lex->token == tk_colon ||
                         lex->token == tk_right_curly ||
                         lex->token == tk_end_tag || lex->token == tk_eof)
                    break;
                else {
                    lily_raise(parser->error,
                        "expression: Unexpected token value %s.\n",
                        tokname(lex->token));
                }
                lily_ast_push_binary_op(parser->ast_pool, op);

                lily_lexer(lex);
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
            lily_raise(parser->error, "%s has already been declared.\n",
                       var->name);

        var = lily_new_var(parser->symtab, cls, lex->label);

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
            lily_raise(parser->error, "Expected ',' or ')', not %s.\n",
                       tokname(lex->token));
        }
        /* else comma, so just jump back up. */
    }
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
            lily_sig *ret_sig = parser->emit->target_ret;
            if (ret_sig != NULL) {
                expression(parser, EX_NEED_VALUE | EX_SINGLE | EX_SAVE_AST);
                lily_emit_return(parser->emit, parser->ast_pool->root, ret_sig);
                lily_ast_reset_pool(parser->ast_pool);
            }
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
                    lily_raise(parser->error, "Expected ':', not %s.\n",
                               tokname(lex->token));

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
                    lily_raise(parser->error, "Expected ':', not %s.\n",
                               tokname(lex->token));

                lily_lexer(lex);
            }
            else if (key_id == KEY_ELSE) {
                lily_emit_clear_block(parser->emit, /*have_else=*/1);

                lily_lexer(lex);
                if (lex->token != tk_colon)
                    lily_raise(parser->error, "Expected ':', not %s.\n",
                               tokname(lex->token));

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

static void parse_method_decl(lily_parse_state *parser)
{
    int i = 0;
    lily_class *cls = lily_class_by_id(parser->symtab, SYM_CLASS_METHOD);
    lily_method_val *m;
    lily_lex_state *lex = parser->lex;
    lily_var *method_var, *save_top;

    m = lily_try_new_method_val(parser->symtab);
    if (m == NULL)
        lily_raise_nomem(parser->error);

    /* Get the method's name. */
    NEED_NEXT_TOK(tk_word)
    /* Form: method name(args):<ret type> {  } */
    method_var = lily_new_var(parser->symtab, cls, lex->label);
    method_var->value.ptr = m;

    NEED_NEXT_TOK(tk_left_parenth)
    save_top = parser->symtab->var_top;

    /* Argument signatures are collected later so that the array doesn't have
       to have a guess-size or be constantly realloc'd. */
    lily_lexer(lex);

    /* If (), then no args and no collecting. Otherwise, start collecting. */
    if (lex->token == tk_right_parenth) {
        m->first_arg = NULL;
        m->last_arg = NULL;
    }
    else {
        while (1) {
            NEED_CURRENT_TOK(tk_word)
            cls = lily_class_by_name(parser->symtab, lex->label);
            if (cls == NULL)
                lily_raise(parser->error,
                           "unknown class name %s.\n", lex->label);

            NEED_NEXT_TOK(tk_word)
            lily_new_var(parser->symtab, cls, lex->label);
            i++;
    
            lily_lexer(lex);
            if (lex->token == tk_comma) {
                lily_lexer(lex);
                continue;
            }
            else if (lex->token == tk_right_parenth)
                break;
            else {
                lily_free(m->code);
                lily_free(m);
                lily_raise(parser->error, "Expected ',' or ')', not %s.\n",
                        tokname(lex->token));
            }
        }
        m->first_arg = save_top->next;
        m->last_arg = parser->symtab->var_top;
    }

    NEED_NEXT_TOK(tk_colon)
    NEED_NEXT_TOK(tk_word)

    lily_call_sig *csig = method_var->sig->node.call;

    cls = lily_class_by_name(parser->symtab, lex->label);
    if (cls != NULL)
        csig->ret = cls->sig;
    else {
        if (strcmp(lex->label, "nil") == 0)
            csig->ret = NULL;
        else
            lily_raise(parser->error, "unknown class name %s.\n", lex->label);
    }

    int j;

    save_top = save_top->next;

    if (i != 0) {
        csig->args = lily_malloc(sizeof(lily_sig *) * i);
        if (csig->args == NULL)
            lily_raise_nomem(parser->error);

        for (j = 0;j < i;j++) {
            csig->args[j] = save_top->sig;
            save_top = save_top->next;
        }
    }
    else
        csig->args = NULL;

    csig->num_args = i;

    lily_emit_enter_method(parser->emit, method_var);
    NEED_NEXT_TOK(tk_left_curly)
    lily_lexer(parser->lex);
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

        if (key_id == KEY_RETURN) {
            lily_sig *ret_sig = parser->emit->target_ret;
            if (ret_sig != NULL) {
                expression(parser, EX_NEED_VALUE | EX_SINGLE | EX_SAVE_AST);
                lily_emit_return(parser->emit, parser->ast_pool->root, ret_sig);
                lily_ast_reset_pool(parser->ast_pool);
            }
        }
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
            if (lclass->id != SYM_CLASS_METHOD)
                declaration(parser, lclass);
            else
                /* Methods have a special kind of declaration. */
                parse_method_decl(parser);
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
    lily_free(parser);
}

lily_parse_state *lily_new_parse_state(lily_excep_data *excep)
{
    lily_parse_state *s = lily_malloc(sizeof(lily_parse_state));

    if (s == NULL)
        return NULL;

    s->ast_pool = lily_ast_init_pool(excep, 8);
    s->error = excep;

    s->symtab = lily_new_symtab(excep);
    s->emit = lily_new_emit_state(excep);
    s->lex = lily_new_lex_state(excep);
    s->vm = lily_new_vm_state(excep);

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

            lily_vm_execute(parser->vm);
            /* Show var values, to verify execution went as expected. */
            lily_show_var_values(parser->symtab);
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
    }
}
