#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "lily_parser.h"

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

    lily_parse_state *parser = lily_new_parse_state();
    if (parser == NULL) {
        fputs("ErrNoMemory: No memory to alloc interpreter.\n", stderr);
        exit(EXIT_FAILURE);
    }

    int i, num_tests;
    num_tests = sizeof(tests) / sizeof(tests[0]);

    for (i = 0;i < num_tests;i++) {
        fprintf(stderr, "Running strload test #%d.\n", i);
        if (lily_parse_string(parser, tests[i]) == 0) {
            lily_raiser *raiser = parser->raiser;
            fprintf(stderr, "%s", lily_name_for_error(raiser->error_code));
            if (raiser->message)
                fprintf(stderr, ": %s", raiser->message);
            else
                fputc('\n', stderr);

            if (parser->mode == pm_parse) {
                int line_num;
                if (raiser->line_adjust == 0)
                    line_num = parser->lex->line_num;
                else
                    line_num = raiser->line_adjust;

                fprintf(stderr, "Where: File \"%s\" at line %d\n",
                        parser->lex->filename, line_num);
            }
            else if (parser->mode == pm_execute) {
                lily_vm_stack_entry **vm_stack;
                lily_vm_stack_entry *entry;
                int i;

                vm_stack = parser->vm->method_stack;
                fprintf(stderr, "Traceback:\n");
                for (i = parser->vm->method_stack_pos-1;i >= 0;i--) {
                    entry = vm_stack[i];
                    fprintf(stderr, "    Method \"%s\" at line %d.\n",
                            ((lily_var *)entry->method)->name, entry->line_num);
                }
            }
            exit(EXIT_FAILURE);
        }
    }

    lily_free_parse_state(parser);
    exit(EXIT_SUCCESS);
}
