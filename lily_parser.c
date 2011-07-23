#include <stdlib.h>
#include <string.h>

#include "lily_lexer.h"
#include "lily_parser.h"
#include "lily_types.h"

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

void lily_init_parser(lily_parser_data *d)
{
    /* Turn keywords into symbols. */
    int i, kw_count;

    kw_count = sizeof(keywords) / sizeof(keywords[0]);
    for (i = 0;i < kw_count;i++) {
        lily_symbol *s = malloc(sizeof(lily_symbol));
        if (s == NULL)
            lily_impl_fatal("Out of memory creating symtab.\n");

        s->sym_name = malloc(strlen(keywords[i].name) + 1);
        if (s->sym_name == NULL)
            lily_impl_fatal("Out of memory creating symtab.\n");

        strcpy(s->sym_name, keywords[i].name);
        s->callable = keywords[i].callable;
        s->num_args = keywords[i].num_args;
        s->code = NULL;
        s->code_len = 0;
        s->code_pos = 0;
        s->next = symtab;
        symtab = s;
    }

    main_func = symtab;
    main_func->code = malloc(sizeof(int) * 4);
    if (main_func->code == NULL)
        lily_impl_fatal("Out of memory creating code section.\n");
    main_func->code_len = 4;
    main_func->code_pos = 0;
}

void lily_parser(void)
{
    tok = lily_lexer_token();

    lily_lexer();
}
