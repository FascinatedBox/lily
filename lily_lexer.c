#include <stdlib.h>
#include <string.h>

#include "lily_lexer.h"
#include "lily_impl.h"
#include "lily_page_scanner.h"

/* Shared by lily_page_scanner.c */
static FILE *lex_file;
static char *lex_buffer;
static int lex_bufsize;
static int lex_bufpos;
static lily_token *lex_token;

static char ch_class[255];
#define CH_INVALID  0
#define CH_WORD     1

void lily_init_lexer(char *filename)
{
    lex_file = fopen(filename, "r");
    if (lex_file == NULL)
        lily_impl_fatal("Couldn't open '%s'.\n", filename);

    lex_buffer = malloc(1024 * sizeof(char));
    if (lex_buffer == NULL)
        lily_impl_fatal("No memory to init lexer.\n");

    lex_token = malloc(sizeof(lily_token));
    if (lex_token == NULL)
        lily_impl_fatal("No memory to create lexer token.\n");

    lex_token->word_buffer = malloc(1024 * sizeof(char));
    if (lex_token->word_buffer == NULL)
        lily_impl_fatal("No memory to create lexer token buffer.\n");

    lex_bufsize = 1023;

    lily_page_data *d = malloc(sizeof(lily_page_data));
    if (d == NULL)
        lily_impl_fatal("No memory to init page scanner.\n");

    d->lex_file = lex_file;
    d->lex_buffer = lex_buffer;
    d->lex_bufsize = &lex_bufsize;
    d->lex_bufpos = &lex_bufpos;

    lily_init_page_scanner(d);
    free(d);

    /* Initialize ch_class, which is used to determine what 'class' a letter
       is in. */
    memset(ch_class, CH_INVALID, sizeof(ch_class));

    int i;
    for (i = 'a';i < 'z';i++)
        ch_class[i] = CH_WORD;

    for (i = 'A';i < 'Z';i++)
        ch_class[i] = CH_WORD;

    ch_class[(int)'_'] = CH_WORD;

    /* Make sure the lexer starts after the <@lily block. */
    lily_page_scanner();
}

lily_token *lily_lexer_token()
{
    return lex_token;
}

void lily_lexer(void)
{

}
