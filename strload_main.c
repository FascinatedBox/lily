#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "lily_interp.h"

/* strload_main.c :
   This makes sure that the lexer can handle a string instead of a file. */

void lily_impl_debugf(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
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
        fputs("Usage : lily_strload\n", stderr);
        exit(EXIT_FAILURE);
    }

    lily_interp *interp = lily_new_interp();
    if (interp == NULL) {
        fputs("ErrNoMemory: No memory to alloc interpreter.\n", stderr);
        exit(EXIT_FAILURE);
    }

    int i, num_tests;
    num_tests = sizeof(tests) / sizeof(tests[0]);

    for (i = 0;i < num_tests;i++) {
        fprintf(stderr, "Running strload test #%d.\n", i);
        if (lily_parse_string(interp, tests[i]) == 0) {
            lily_parse_state *parser = interp->parser;
            lily_excep_data *error = interp->error;
            fprintf(stderr, "%s", lily_name_for_error(error->error_code));
            if (error->message)
                fprintf(stderr, ": %s", error->message);
            else
                fputc('\n', stderr);

            if (parser->mode == pm_parse) {
                int line_num;
                if (interp->error->line_adjust == 0)
                    line_num = interp->parser->lex->line_num;
                else
                    line_num = interp->error->line_adjust;

                fprintf(stderr, "Where: File \"%s\" at line %d\n",
                        interp->parser->lex->filename, line_num);
            }
            exit(EXIT_FAILURE);
        }
    }

    lily_free_interp(interp);
    exit(EXIT_SUCCESS);
}
