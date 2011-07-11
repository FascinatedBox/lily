#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "lily_page_scanner.h"

/* fake_server_main.c :
 * Since lily will be run from a server for most of the time, this emulates a
 * server...kind of. */

/* The page scanner uses this to send HTML chunks out. Since this isn't a real
   server, it does nothing. */
void lily_be_send_html(char *htmldata)
{

}

/* A fatal error somewhere. */
void lily_be_fatal(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        lily_be_fatal("Usage : fake_server_lily <filename>\n");
    }
    lily_page_scanner_init(argv[1]);
    lily_page_scanner();
    exit(EXIT_SUCCESS);
}
