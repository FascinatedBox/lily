#include <stdlib.h>
#include <string.h>

#include "lily_ast.h"
#include "lily_lexer.h"
#include "lily_parser.h"
#include "lily_types.h"
#include "lily_impl.h"
#include "lily_emitter.h"

static lily_token *tok;
/* Where all code outside of functions gets stored. */
static lily_symbol *main_func;

lily_symbol *symtab = NULL;

struct lily_keyword {
    char *name;
    int callable;
    int num_args;
} keywords[] =
{
    {"str", 0, 0},
    {"print", 1, 1},
    /* All code outside of functions is stuffed here, and at the end of parsing,
       this function is called. */
    {"", 1, 1}
};

typedef struct {
    int depth;
    int *num_expected;
    lily_ast **saved_trees;
    lily_ast *current_tree;
    int num_args;
} expr_data;

expr_data *expr_state;

static void clear_expr_state(void)
{
    expr_state->depth = 0;
    expr_state->num_args = 0;
    expr_state->current_tree = NULL;
    expr_state->saved_trees = lily_impl_malloc(sizeof(lily_ast *) * 32);

    int i;
    for (i = 0;i < 32;i++)
        expr_state->saved_trees[i] = NULL;
    memset(expr_state->num_expected, 0, 32);
}

static void enter_parenth(int args_needed)
{
    expr_state->num_expected[expr_state->depth] =
        expr_state->num_args + args_needed;
    expr_state->depth++;
    expr_state->current_tree = NULL;
}

/* Might want to look into merging initializers later. */
static void init_builtin_symbol(lily_symbol *s)
{
    s->code_data = NULL;
    s->val_type = vt_builtin;
    s->sym_value = NULL;
    s->next = symtab;
    symtab = s;
}

static void init_temp_symbol(lily_symbol *s)
{
    s->code_data = NULL;
    s->sym_value = NULL;
    s->next = symtab;
    symtab = s;
}

void lily_init_parser(lily_parser_data *d)
{
    /* Turn keywords into symbols. */
    int i, kw_count;

    kw_count = sizeof(keywords) / sizeof(keywords[0]);
    for (i = 0;i < kw_count;i++) {
        lily_symbol *s = lily_impl_malloc(sizeof(lily_symbol));
        init_builtin_symbol(s);

        s->sym_name = lily_impl_malloc(strlen(keywords[i].name) + 1);

        strcpy(s->sym_name, keywords[i].name);
        s->callable = keywords[i].callable;
        s->num_args = keywords[i].num_args;
    }

    main_func = symtab;

    lily_code_data *cd = lily_impl_malloc(sizeof(lily_code_data));
    cd->code = lily_impl_malloc(sizeof(int) * 4);
    cd->code_len = 4;
    cd->code_pos = 0;

    main_func->code_data = cd;

    expr_state = lily_impl_malloc(sizeof(expr_data));
    expr_state->num_expected = lily_impl_malloc(sizeof(int) * 32);

    clear_expr_state();
}

static lily_symbol *find_symbol(char *name)
{
    lily_symbol *sym;

    sym = symtab;
    while (sym != NULL) {
        if (strcmp(sym->sym_name, name) == 0)
            return sym;
        sym = sym->next;
    }
    return NULL;
}

static void parse_expr_value(void)
{
    if (tok->tok_type == tk_word) {
        lily_symbol *sym = find_symbol(tok->word_buffer);

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
        lily_symbol *sym = lily_impl_malloc(sizeof(lily_symbol));
        init_temp_symbol(sym);

        lily_strval *strval = lily_impl_malloc(sizeof(lily_strval));
        int str_size = strlen(tok->word_buffer);
        char *str = lily_impl_malloc(str_size + 1);
        strcpy(str, tok->word_buffer);
        strval->str = str;
        strval->str_size = str_size;

        lily_ast *ast = lily_ast_init_var(sym);

        sym->val_type = vt_str;
        sym->sym_value = strval;
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

    lily_lexer();
    if (tok->tok_type == tk_word)
        parse_statement();
}
