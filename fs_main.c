#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "lily_lexer.h"
#include "lily_parser.h"
#include "lily_emitter.h"
#include "lily_symtab.h"

/* fs_main.c :
 * Since lily will be run from a server for most of the time, this emulates a
 * server...kind of. */

/* The page scanner uses this to send HTML chunks out. Since this isn't a real
   server, it does nothing. */
void lily_impl_send_html(char *htmldata)
{

}

/* A fatal error somewhere. */
void lily_impl_fatal(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
    exit(EXIT_FAILURE);
}

void *lily_impl_malloc(size_t memsize)
{
    void *chunk = malloc(memsize);
    if (chunk == NULL)
        /* Report the size too, in case that's the reason. */
        lily_impl_fatal("Failed to malloc a chunk of size %d.\n", memsize);
    
    return chunk;
}

void *lily_impl_realloc(void *chunk, size_t memsize)
{
    void *newchunk = realloc(chunk, memsize);
    if (newchunk == NULL)
        lily_impl_fatal("Failed to realloc a chunk to size %d.\n", memsize);

    return newchunk;
}

int main(int argc, char **argv)
{
    if (argc != 2)
        lily_impl_fatal("Usage : lily_fs <filename>\n");

    lily_interp *itp = lily_init_interp();
    lily_include(itp->lex_data, argv[1]);
    lily_parser(itp);

    exit(EXIT_SUCCESS);
}
