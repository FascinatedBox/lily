#include <string.h>

#include "lily_interp.h"
#include "lily_ast.h"
#include "lily_lexer.h"
#include "lily_symtab.h"
#include "lily_impl.h"
#include "lily_emitter.h"
#include "lily_debug.h"

typedef struct {
    int depth;
    int *num_expected;
    lily_ast_pool *ast_pool;
    lily_ast **saved_trees;
    lily_ast *current_tree;
    int num_args;
    lily_emit_state *em_state;
} parser_data;

static void enter_parenth(parser_data *pr_data, int args_needed)
{
    pr_data->num_expected[pr_data->depth] = pr_data->num_args + args_needed;
    pr_data->depth++;
    pr_data->current_tree = NULL;
}

static parser_data *init_parser_data(lily_interp *interp)
{
    parser_data *pr_data = lily_impl_malloc(sizeof(parser_data));
    pr_data->saved_trees = lily_impl_malloc(sizeof(lily_ast *) * 32);
    pr_data->num_expected = lily_impl_malloc(sizeof(int) * 32);
    pr_data->ast_pool = lily_ast_init_pool(32);
    pr_data->depth = 0;
    pr_data->num_args = 0;
    pr_data->current_tree = NULL;

    pr_data->em_state = lily_init_emit_state(interp);
    lily_emit_set_target(pr_data->em_state, interp->main_func);
    return pr_data;
}

static void free_parser_data(parser_data *pr_data)
{
    lily_ast_free_pool(pr_data->ast_pool);
    lily_free_emit_state(pr_data->em_state);
    free(pr_data->saved_trees);
    free(pr_data->num_expected);
    free(pr_data);
}

static void parse_expr_value(lily_interp *interp, parser_data *pr_data)
{
    lily_token *token = interp->lex_data->token;

    if (token->tok_type == tk_word) {
        lily_symbol *sym;
        sym = lily_st_find_symbol(interp->symtab, token->word_buffer);

        if (sym == NULL)
            lily_impl_fatal("Variable '%s' is undefined.\n",
                            token->word_buffer);

        if (sym->callable) {
            /* New trees will get saved to the args section of this tree
                when they are done. */
            lily_ast *ast = lily_ast_init_call(pr_data->ast_pool, sym);
            pr_data->saved_trees[pr_data->depth] = ast;

            enter_parenth(pr_data, sym->num_args);

            lily_lexer(interp->lex_data);
            if (token->tok_type != tk_left_parenth)
                lily_impl_fatal("Expected '(' after function name.\n");

            lily_lexer(interp->lex_data);
            /* This handles the first value of the function. */
            parse_expr_value(interp, pr_data);
        }
        else {
            lily_ast *ast = lily_ast_init_var(pr_data->ast_pool, sym);
            if (pr_data->current_tree == NULL)
                pr_data->current_tree = ast;

            lily_lexer(interp->lex_data);
        }
    }
    else if (token->tok_type == tk_double_quote) {
        lily_symbol *sym = lily_st_new_str_sym(interp, token->word_buffer);
        lily_ast *ast = lily_ast_init_var(pr_data->ast_pool, sym);

        pr_data->current_tree =
            lily_ast_merge_trees(pr_data->current_tree, ast);

        lily_lexer(interp->lex_data);
    }
    else if (token->tok_type == tk_num_int) {
        lily_symbol *sym = lily_st_new_int_sym(interp, token->int_val);
        lily_ast *ast = lily_ast_init_var(pr_data->ast_pool, sym);

        pr_data->current_tree =
            lily_ast_merge_trees(pr_data->current_tree, ast);

        lily_lexer(interp->lex_data);
    }
    else if (token->tok_type == tk_num_dbl) {
        lily_symbol *sym = lily_st_new_dbl_sym(interp, token->dbl_val);
        lily_ast *ast = lily_ast_init_var(pr_data->ast_pool, sym);

        pr_data->current_tree =
            lily_ast_merge_trees(pr_data->current_tree, ast);

        lily_lexer(interp->lex_data);
    }
}

/* Expressions are divided into two states:
 * Value: A value is needed. ( is handled, because parenth expressions always
   yield a single value).
 * Op: The expression has enough values. Getting an op means another value will
   be necessary. If a word is found, it is the first word of the next
   expression (so no semicolons). ) is handled, because the expression is done
   and can properly be closed. */

static void parse_expr_top(lily_interp *interp, parser_data *pr_data)
{
    lily_token *token = interp->lex_data->token;

    while (1) {
        parse_expr_value(interp, pr_data);
        if (token->tok_type == tk_equal) {
            lily_ast *lhs = pr_data->current_tree;
            lily_ast *op = lily_ast_init_binary_op(pr_data->ast_pool, tk_equal);

            pr_data->current_tree = lily_ast_merge_trees(lhs, op);

            lily_lexer(interp->lex_data);
        }
        else if (token->tok_type == tk_right_parenth) {
            if (pr_data->current_tree == NULL)
                lily_impl_fatal("')' but current tree is NULL!\n");

            lily_ast *a = pr_data->saved_trees[pr_data->depth - 1];
            if (a->expr_type == func_call)
                lily_ast_add_arg(pr_data->ast_pool, a,
                                 pr_data->current_tree);

            /* todo : Check proper # of arguments. */
            pr_data->current_tree = a;
            pr_data->depth--;

            /* This should either be a binary op, or the first word of the next
               expression. Ready the token. */
            lily_lexer(interp->lex_data);
        }
        else if (token->tok_type == tk_word) {
            /* todo : Check balance of ( and ). */
            break;
        }
        else if (token->tok_type == tk_end_tag)
            break;
        else {
            lily_impl_fatal("parse_expr_top: Unexpected token value %d.\n",
                            token->tok_type);
        }
    }
};

static void parse_statement(lily_interp *interp, parser_data *pr_data)
{
    char *word_buffer = interp->lex_data->token->word_buffer;

    /* todo : Check for a proper tree. */
    if (lily_st_find_symbol(interp->symtab, word_buffer) == NULL)
        lily_st_new_var_sym(interp, word_buffer);

    parse_expr_top(interp, pr_data);
    lily_emit_ast(pr_data->em_state, pr_data->current_tree);
    lily_ast_reset_pool(pr_data->ast_pool);
    pr_data->current_tree = NULL;
}

void lily_parser(lily_interp *interp)
{
    parser_data *pr_data = init_parser_data(interp);
    lily_token *token = interp->lex_data->token;

    lily_lexer(interp->lex_data);

    while (1) {
        if (token->tok_type == tk_word)
            parse_statement(interp, pr_data);
        else if (token->tok_type == tk_end_tag) {
            /* Execute the code, eat html, then go back to collection. */
            lily_emit_vm_return(pr_data->em_state);
            /* Show symtab until the bugs are gone. */
            lily_show_symtab(interp->symtab);

            lily_vm_execute(interp->main_func);

            lily_lexer_handle_page_data(interp->lex_data);
            if (token->tok_type == tk_eof)
                break;
        }
    }

    free_parser_data(pr_data);
}
