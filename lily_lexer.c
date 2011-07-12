#include "lily_lexer.h"

/* Shared by lily_page_scanner.c */
static FILE *lex_file;
static char *lex_buffer;
static int lex_bufsize;
static int lex_bufpos;

void lily_init_lexer(lily_lexer_data *d)
{
    lex_file = d->lex_file;
    lex_buffer = d->lex_buffer;
    lex_bufsize = *d->lex_bufsize;
    lex_bufpos = *d->lex_bufpos;
}
