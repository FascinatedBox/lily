#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "lily_interp.h"

/* fs_main.c :
 * Since lily will be run from a server for most of the time, this emulates a
 * server...kind of. */
void lily_impl_debugf(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
}

/* The page scanner uses this to send HTML chunks out. Since this isn't a real
   server, it does nothing. */
void lily_impl_send_html(char *htmldata)
{

}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fputs("Usage : lily_fs <filename>\n", stderr);
        exit(EXIT_FAILURE);
    }

    lily_interp *interp = lily_new_interp();
    if (interp == NULL) {
        fputs("Error: Out of memory.", stderr);
        exit(EXIT_FAILURE);
    }

    if (lily_parse_file(interp, argv[1]) == 0) {
        fputs(interp->error->message, stderr);
        exit(EXIT_FAILURE);
    }

    lily_free_interp(interp);
    exit(EXIT_SUCCESS);
}
