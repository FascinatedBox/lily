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

    expr_state->depth = 0;
    expr_state->num_args = 0;
    expr_state->current_tree = NULL;

    int i;
    for (i = 0;i < 32;i++)
        expr_state->saved_trees[i] = NULL;
    memset(expr_state->num_expected, 0, 32);
}

static void parse_expr_value(void)
{
    if (tok->tok_type == tk_word) {
        lily_symbol *sym = lily_st_find_symbol(tok->word_buffer);

        if (sym != NULL) {
            if (sym->callable) {
                /* New trees will get saved to the args section of this tree
                   when they are done. */
                lily_ast *ast = lily_ast_init_call(sym);
                expr_state->saved_trees[expr_state->depth] = ast;

                enter_parenth(sym->num_args);

                lily_lexer();
                if (tok->tok_type != tk_left_parenth)
                    lily_impl_fatal("Expected '(' after function name.\n");

                lily_lexer();
                parse_expr_value();
            }
        }
    }
    else if (tok->tok_type == tk_double_quote) {
        lily_symbol *sym = lily_st_new_str_sym(tok->word_buffer);
        lily_ast *ast = lily_ast_init_var(sym);

        expr_state->current_tree = ast;

        lily_lexer();
        parse_expr_value();
    }
    else if (tok->tok_type == tk_right_parenth) {
        if (expr_state->current_tree == NULL)
            lily_impl_fatal("')' but current tree is NULL!\n");

        lily_ast *a = expr_state->saved_trees[expr_state->depth - 1];
        if (a->expr_type == func_call)
            lily_ast_add_arg(a, expr_state->current_tree);

        /* todo : Check proper # of arguments. */
        expr_state->current_tree = a;
        expr_state->depth--;
    }
}

static void parse_expr_binary(void)
{
    parse_expr_value();
};

static void parse_statement(void)
{
    /* todo : Check for a proper tree. */
    parse_expr_binary();
}

void lily_parser(void)
{
    tok = lily_lexer_token();

    while (1) {
        lily_lexer();
        if (tok->tok_type == tk_word)
            parse_statement();
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
}
