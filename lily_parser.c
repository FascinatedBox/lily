#include <string.h>

#include "lily_ast.h"
#include "lily_lexer.h"
#include "lily_symtab.h"
#include "lily_impl.h"
#include "lily_emitter.h"

static lily_token *tok;

typedef struct {
    int depth;
    int *num_expected;
    lily_ast_pool *ast_pool;
    lily_ast **saved_trees;
    lily_ast *current_tree;
    int num_args;
} expr_data;

expr_data *expr_state;

static void enter_parenth(int args_needed)
{
    expr_state->num_expected[expr_state->depth] =
        expr_state->num_args + args_needed;
    expr_state->depth++;
    expr_state->current_tree = NULL;
}

void lily_init_parser(void)
{
    expr_state = lily_impl_malloc(sizeof(expr_data));
    expr_state->saved_trees = lily_impl_malloc(sizeof(lily_ast *) * 32);
    expr_state->num_expected = lily_impl_malloc(sizeof(int) * 32);

    expr_state->ast_pool = lily_ast_init_pool(32);
    expr_state->depth = 0;
    expr_state->num_args = 0;
    expr_state->current_tree = NULL;
}

static void parse_expr_value(void)
{
    if (tok->tok_type == tk_word) {
        lily_symbol *sym = lily_st_find_symbol(tok->word_buffer);

        if (sym != NULL) {
            if (sym->callable) {
                /* New trees will get saved to the args section of this tree
                   when they are done. */
                lily_ast *ast = lily_ast_init_call(expr_state->ast_pool, sym);
                expr_state->saved_trees[expr_state->depth] = ast;

                enter_parenth(sym->num_args);

                lily_lexer();
                if (tok->tok_type != tk_left_parenth)
                    lily_impl_fatal("Expected '(' after function name.\n");

                lily_lexer();
                /* This handles the first value of the function. */
                parse_expr_value();
            }
        }
        else {
            lily_symbol *sym = lily_st_new_var_sym(tok->word_buffer);
            lily_ast *ast = lily_ast_init_var(expr_state->ast_pool, sym);
            if (expr_state->current_tree == NULL)
                expr_state->current_tree = ast;

            lily_lexer();
        }
    }
    else if (tok->tok_type == tk_double_quote) {
        lily_symbol *sym = lily_st_new_str_sym(tok->word_buffer);
        lily_ast *ast = lily_ast_init_var(expr_state->ast_pool, sym);

        expr_state->current_tree = ast;

        lily_lexer();
    }
    else if (tok->tok_type == tk_num_int) {
        lily_symbol *sym = lily_st_new_int_sym(tok->int_val);
        lily_ast *ast = lily_ast_init_var(expr_state->ast_pool, sym);

        expr_state->current_tree = ast;

        lily_lexer();
    }
    else if (tok->tok_type == tk_num_dbl) {
        lily_symbol *sym = lily_st_new_dbl_sym(tok->dbl_val);
        lily_ast *ast = lily_ast_init_var(expr_state->ast_pool, sym);

        expr_state->current_tree = ast;

        lily_lexer();
    }
}

/* Expressions are divided into two states:
 * Value: A value is needed. ( is handled, because parenth expressions always
   yield a single value).
 * Op: The expression has enough values. Getting an op means another value will
   be necessary. If a word is found, it is the first word of the next
   expression (so no semicolons). ) is handled, because the expression is done
   and can properly be closed. */

static void parse_expr_top(void)
{
    while (1) {
        parse_expr_value();
        if (tok->tok_type == tk_equal) {
            lily_ast *a = expr_state->current_tree;
            lily_ast *bt;

            bt = lily_ast_init_binary_op(expr_state->ast_pool, tk_equal);
            expr_state->current_tree = lily_ast_merge_trees(a, bt);

            lily_lexer();
        }
        else if (tok->tok_type == tk_right_parenth) {
            if (expr_state->current_tree == NULL)
                lily_impl_fatal("')' but current tree is NULL!\n");

            lily_ast *a = expr_state->saved_trees[expr_state->depth - 1];
            if (a->expr_type == func_call)
                lily_ast_add_arg(expr_state->ast_pool, a,
                                 expr_state->current_tree);

            /* todo : Check proper # of arguments. */
            expr_state->current_tree = a;
            expr_state->depth--;

            /* This should either be a binary op, or the first word of the next
               expression. Ready the token. */
            lily_lexer();
        }
        else if (tok->tok_type == tk_word) {
            /* todo : Check balance of ( and ). */
            break;
        }
        else if (tok->tok_type == tk_end_tag)
            break;
        else {
            lily_impl_fatal("parse_expr_top: Unexpected token value %d.\n",
                tok->tok_type);
        }
    }
};

static void parse_statement(void)
{
    /* todo : Check for a proper tree. */
    parse_expr_top();
}

void lily_parser(void)
{
    tok = lily_lexer_token();
    lily_lexer();

    while (1) {
        if (tok->tok_type == tk_word) {
            parse_statement();
            lily_ast_reset_pool(expr_state->ast_pool);
        }
        else if (tok->tok_type == tk_end_tag) {
            /* Execute the code, eat html, then go back to collection. */
            lily_emit_ast(main_func, expr_state->current_tree);
            lily_emit_vm_return(main_func);
            lily_vm_execute(main_func);

            lily_lexer_handle_page_data();
            if (tok->tok_type == tk_eof)
                break;
        }
    }

    lily_ast_free_pool(expr_state->ast_pool);
}
