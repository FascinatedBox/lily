#include <string.h>

#include "lily_ast.h"
#include "lily_lexer.h"
#include "lily_symtab.h"
#include "lily_impl.h"
#include "lily_emitter.h"
#include "lily_parser.h"
#include "lily_debug.h"

static void enter_parenth(lily_parse_state *parser, int args_needed)
{
    parser->num_expected[parser->depth] = parser->num_args + args_needed;
    parser->depth++;
    parser->current_tree = NULL;
}

lily_parse_state *lily_new_parse_state(lily_excep_data *excep)
{
    lily_parse_state *s = lily_malloc(sizeof(lily_parse_state));

    if (s == NULL)
        return NULL;

    s->saved_trees = lily_malloc(sizeof(lily_ast *) * 32);
    s->num_expected = lily_malloc(sizeof(int) * 32);
    s->ast_pool = lily_ast_init_pool(excep, 1);
    s->depth = 0;
    s->num_args = 0;
    s->current_tree = NULL;
    s->error = excep;

    s->symtab = lily_new_symtab(excep);
    s->emit = lily_new_emit_state(excep);
    s->lex = lily_new_lex_state(excep);

    if (s->lex == NULL || s->emit == NULL || s->symtab == NULL ||
        s->ast_pool == NULL || s->num_expected == NULL ||
        s->saved_trees == NULL) {
        lily_free(s->saved_trees);
        lily_free(s->num_expected);
        if (s->symtab != NULL)
            lily_free_symtab(s->symtab);
        if (s->emit != NULL)
            lily_free_emit_state(s->emit);
        if (s->lex != NULL)
            lily_free_lex_state(s->lex);
        if (s->ast_pool != NULL)
            lily_ast_free_pool(s->ast_pool);
        lily_free(s);
        return NULL;
    }

    s->symtab->lex_linenum = &s->lex->line_num;
    lily_emit_set_target(s->emit, s->symtab->main);
    return s;
}

void lily_free_parse_state(lily_parse_state *parser)
{
    lily_ast_free_pool(parser->ast_pool);
    lily_free_symtab(parser->symtab);
    lily_free_lex_state(parser->lex);
    lily_free_emit_state(parser->emit);
    lily_free(parser->saved_trees);
    lily_free(parser->num_expected);
    lily_free(parser);
}

static void parse_expr_value(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_symtab *symtab = parser->symtab;

    while (1) {
        if (lex->token == tk_word) {
            lily_symbol *sym = lily_sym_by_name(symtab, lex->label);

            if (sym == NULL)
                lily_raise(parser->error, err_syntax,
                    "Variable '%s' is undefined.\n", lex->label);

            if (isafunc(sym)) {
                /* New trees will get saved to the args section of this tree
                    when they are done. */
                lily_ast *ast = lily_ast_init_call(parser->ast_pool, sym);
                parser->saved_trees[parser->depth] = ast;

                enter_parenth(parser, sym->num_args);

                lily_lexer(parser->lex);
                if (lex->token != tk_left_parenth)
                    lily_raise(parser->error, err_syntax,
                        "Expected '(' after function name.\n");

                lily_lexer(lex);
                /* Get the first value of the function. */
                continue;
            }
            else {
                lily_ast *ast = lily_ast_init_var(parser->ast_pool, sym);
                if (parser->current_tree == NULL)
                    parser->current_tree = ast;

                lily_lexer(lex);
            }
        }
        else if (lex->token == tk_double_quote) {
            lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_STR);
            lily_symbol *sym = lily_new_temp(symtab, cls, lex->value);
            lily_ast *ast = lily_ast_init_var(parser->ast_pool, sym);

            parser->current_tree =
                lily_ast_merge_trees(parser->current_tree, ast);

            lily_lexer(lex);
        }
        else if (lex->token == tk_integer) {
            lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_STR);
            lily_symbol *sym = lily_new_temp(symtab, cls, lex->value);
            lily_ast *ast = lily_ast_init_var(parser->ast_pool, sym);

            parser->current_tree =
                lily_ast_merge_trees(parser->current_tree, ast);

            lily_lexer(lex);
        }
        else if (lex->token == tk_number) {
            lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_STR);
            lily_symbol *sym = lily_new_temp(symtab, cls, lex->value);
            lily_ast *ast = lily_ast_init_var(parser->ast_pool, sym);

            parser->current_tree =
                lily_ast_merge_trees(parser->current_tree, ast);

            lily_lexer(lex);
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

static void parse_expr_top(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;

    while (1) {
        if (lex->token == tk_equal) {
            lily_ast *lhs = parser->current_tree;
            lily_ast *op = lily_ast_init_binary_op(parser->ast_pool, tk_equal);

            parser->current_tree = lily_ast_merge_trees(lhs, op);
            if (parser->current_tree == NULL)
                lily_raise(parser->error, err_stub, "Handle two tree merge.\n");

            lily_lexer(lex);
        }
        else if (lex->token == tk_right_parenth) {
            if (parser->current_tree == NULL)
                lily_raise(parser->error, err_internal,
                    "')' but current tree is NULL!\n");

            lily_ast *a = parser->saved_trees[parser->depth - 1];
            if (a->expr_type == func_call)
                lily_ast_add_arg(parser->ast_pool, a,
                                 parser->current_tree);

            /* todo : Check proper # of arguments. */
            parser->current_tree = a;
            parser->depth--;

            lily_lexer(lex);
            /* If no functions, and a word, then the word is the start of the
               next statement. */
            if (parser->depth == 0 && lex->token == tk_word)
                break;
        }
        else if (lex->token == tk_word) {
            if (parser->depth != 0)
                lily_raise(parser->error, err_syntax,
                           "Expected ')' or a binary op, not a label.\n");

            break;
        }
        else if (lex->token == tk_end_tag)
            break;
        else {
            lily_raise(parser->error, err_stub,
                "parse_expr_top: Unexpected token value %s.\n",
                tokname(lex->token));
        }

        parse_expr_value(parser);
    }

    lily_emit_ast(parser->emit, parser->current_tree);
    lily_ast_reset_pool(parser->ast_pool);
    parser->current_tree = NULL;
}

static void parse_declaration(lily_parse_state *parser, lily_class *cls)
{
    lily_lex_state *lex = parser->lex;
    lily_symbol *sym;

    while (1) {
        /* This starts at the class name, or the comma. The label is next. */
        lily_lexer(lex);

        if (lex->token != tk_word)
            lily_raise(parser->error, err_syntax,
                       "Expected a variable name, not %s.\n",
                       tokname(lex->token));

        sym = lily_sym_by_name(parser->symtab, lex->label);
        if (sym != NULL)
            lily_raise(parser->error, err_syntax,
                       "%s has already been declared.\n", sym->name);

        sym = lily_new_var(parser->symtab, cls, lex->label);

        lily_lexer(parser->lex);
        /* Handle an initializing assignment, if there is one. */
        if (lex->token == tk_equal) {
            parser->current_tree = lily_ast_init_var(parser->ast_pool, sym);
            parse_expr_top(parser);
        }

        /* This is the start of the next statement. */
        if (lex->token == tk_word || lex->token == tk_end_tag)
            break;
        else if (lex->token != tk_comma) {
            lily_raise(parser->error, err_syntax,
                       "Expected ',' or ')', not %s.\n",
                       tokname(lex->token));
        }
        /* else comma, so just jump back up. */
    }
}

static void parse_statement(lily_parse_state *parser)
{
    char *label = parser->lex->label;
    lily_class *lclass;

    lclass = lily_class_by_name(parser->symtab, label);

    if (lclass != NULL) {
        /* Do decl parsing, which will handle any assignments. */
        parse_declaration(parser, lclass);
    }
    else {
        /* statement like 'print(x)', or 'a = b'. Call unary to prep the tree,
           then binary handles the rest. */
        parse_expr_value(parser);
        parse_expr_top(parser);
    }
}

void lily_parser(lily_parse_state *parser)
{
    lily_lex_state *lex = parser->lex;
    lily_lexer(lex);

    while (1) {
        if (lex->token == tk_word)
            parse_statement(parser);
        else if (lex->token == tk_end_tag) {
            /* Execute the code, eat html, then go back to collection. */
            lily_emit_vm_return(parser->emit);
            /* Show symtab until the bugs are gone. */
            lily_show_symtab(parser->symtab);

            lily_vm_execute(parser->error, parser->symtab->main);

            lily_lexer_handle_page_data(parser->lex);
            if (lex->token == tk_eof)
                break;
        }
    }
}
