#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "lily_interp.h"

/* tester_main.c :
 * This sends a bunch of strings to lily to make sure stuff works. */

void lily_impl_debugf(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
}

void lily_impl_send_html(char *htmldata)
{

}

static char *tests[] =
{
    "integer a\n"
    "a = 1"
};

int main(int argc, char **argv)
{
    if (argc != 1) {
        fputs("Usage : lily_tester\n", stderr);
        exit(EXIT_FAILURE);
    }

    lily_interp *interp = lily_new_interp();
    if (interp == NULL) {
        fputs("Error: Out of memory.", stderr);
        exit(EXIT_FAILURE);
    }

    int i, num_tests;
    num_tests = sizeof(tests) / sizeof(tests[0]);

    for (i = 0;i < num_tests;i++) {
        fprintf(stderr, "Running test #%d.\n", i);
        if (lily_parse_string(interp, tests[i]) == 0) {
            fputs(interp->error->message, stderr);
            exit(EXIT_FAILURE);
        }
    }

    lily_free_interp(interp);
    exit(EXIT_SUCCESS);
}
