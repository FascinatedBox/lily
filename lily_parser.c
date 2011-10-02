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

    s->saved_trees = lily_malloc(sizeof(lily_ast *) * 32);
    s->num_expected = lily_malloc(sizeof(int) * 32);
    s->ast_pool = lily_ast_init_pool(32);
    s->depth = 0;
    s->num_args = 0;
    s->current_tree = NULL;
    s->error = excep;

    s->symtab = lily_new_symtab(excep);
    s->emit = lily_new_emit_state(excep);
    s->lex = lily_new_lex_state(excep);

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
    lily_token *token = parser->lex->token;
    lily_symtab *symtab = parser->symtab;

    if (token->tok_type == tk_word) {
        lily_symbol *sym;
        sym = lily_st_find_symbol(symtab, token->word_buffer);

        if (sym == NULL)
            lily_raise(parser->error, err_syntax,
                "Variable '%s' is undefined.\n", token->word_buffer);

        if (sym->callable) {
            /* New trees will get saved to the args section of this tree
                when they are done. */
            lily_ast *ast = lily_ast_init_call(parser->ast_pool, sym);
            parser->saved_trees[parser->depth] = ast;

            enter_parenth(parser, sym->num_args);

            lily_lexer(parser->lex);
            if (token->tok_type != tk_left_parenth)
                lily_raise(parser->error, err_syntax,
                    "Expected '(' after function name.\n");

            lily_lexer(parser->lex);
            /* This handles the first value of the function. */
            parse_expr_value(parser);
        }
        else {
            lily_ast *ast = lily_ast_init_var(parser->ast_pool, sym);
            if (parser->current_tree == NULL)
                parser->current_tree = ast;

            lily_lexer(parser->lex);
        }
    }
    else if (token->tok_type == tk_double_quote) {
        lily_symbol *sym = lily_st_new_str_sym(symtab, token->word_buffer);
        lily_ast *ast = lily_ast_init_var(parser->ast_pool, sym);

        parser->current_tree =
            lily_ast_merge_trees(parser->current_tree, ast);

        lily_lexer(parser->lex);
    }
    else if (token->tok_type == tk_num_int) {
        lily_symbol *sym = lily_st_new_int_sym(symtab, token->int_val);
        lily_ast *ast = lily_ast_init_var(parser->ast_pool, sym);

        parser->current_tree =
            lily_ast_merge_trees(parser->current_tree, ast);

        lily_lexer(parser->lex);
    }
    else if (token->tok_type == tk_num_dbl) {
        lily_symbol *sym = lily_st_new_dbl_sym(symtab, token->dbl_val);
        lily_ast *ast = lily_ast_init_var(parser->ast_pool, sym);

        parser->current_tree =
            lily_ast_merge_trees(parser->current_tree, ast);

        lily_lexer(parser->lex);
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
    lily_token *token = parser->lex->token;

    while (1) {
        parse_expr_value(parser);
        if (token->tok_type == tk_equal) {
            lily_ast *lhs = parser->current_tree;
            lily_ast *op = lily_ast_init_binary_op(parser->ast_pool, tk_equal);

            parser->current_tree = lily_ast_merge_trees(lhs, op);
            if (parser->current_tree == NULL)
                lily_raise(parser->error, err_stub, "Handle two tree merge.\n");

            lily_lexer(parser->lex);
        }
        else if (token->tok_type == tk_right_parenth) {
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

            /* This should either be a binary op, or the first word of the next
               expression. Ready the token. */
            lily_lexer(parser->lex);
        }
        else if (token->tok_type == tk_word) {
            /* todo : Check balance of ( and ). */
            break;
        }
        else if (token->tok_type == tk_end_tag)
            break;
        else {
            lily_raise(parser->error, err_stub,
                "parse_expr_top: Unexpected token value %d.\n",
                token->tok_type);
        }
    }
};

static void parse_statement(lily_parse_state *parser)
{
    char *word_buffer = parser->lex->token->word_buffer;

    /* todo : Check for a proper tree. */
    if (lily_st_find_symbol(parser->symtab, word_buffer) == NULL)
        lily_st_new_var_sym(parser->symtab, word_buffer);

    parse_expr_top(parser);
    lily_emit_ast(parser->emit, parser->current_tree);
    lily_ast_reset_pool(parser->ast_pool);
    parser->current_tree = NULL;
}

void lily_parser(lily_parse_state *parser)
{
    lily_token *token = parser->lex->token;

    lily_lexer(parser->lex);

    while (1) {
        if (token->tok_type == tk_word)
            parse_statement(parser);
        else if (token->tok_type == tk_end_tag) {
            /* Execute the code, eat html, then go back to collection. */
            lily_emit_vm_return(parser->emit);
            /* Show symtab until the bugs are gone. */
            lily_show_symtab(parser->symtab);

            lily_vm_execute(parser->symtab->main);

            lily_lexer_handle_page_data(parser->lex);
            if (token->tok_type == tk_eof)
                break;
        }
    }
}
