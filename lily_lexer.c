#include <stdlib.h>

#include "lily_lexer.h"
#include "lily_impl.h"
#include "lily_page_scanner.h"

/* Shared by lily_page_scanner.c */
static FILE *lex_file;
static char *lex_buffer;
static int lex_bufsize;
static int lex_bufpos;

void lily_init_lexer(char *filename)
{
    lex_file = fopen(filename, "r");
    if (lex_file == NULL)
        lily_impl_fatal("Couldn't open '%s'.\n", filename);

    lex_buffer = malloc(1024 * sizeof(char));
    if (lex_buffer == NULL)
        lily_impl_fatal("No memory to init lexer.\n");
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
}
