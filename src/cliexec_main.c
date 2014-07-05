#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "lily_parser.h"

/* cliexec_main.c :
   This starts an interpreter with a string given as a command-line argument.
   This is a nice tool to quickly check a piece of code, or to toy with the
   language without editing a file. */

void lily_impl_debugf(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}

void lily_impl_puts(char *text)
{
    fputs(text, stdout);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fputs("Usage : lily_cliexec <str>\n", stderr);
        exit(EXIT_FAILURE);
    }

    lily_parse_state *parser = lily_new_parse_state(argc, argv);
    if (parser == NULL) {
        fputs("ErrNoMemory: No memory to alloc interpreter.\n", stderr);
        exit(EXIT_FAILURE);
    }

    if (lily_parse_string(parser, argv[1]) == 0) {
        lily_raiser *raiser = parser->raiser;
        fprintf(stderr, "%s", lily_name_for_error(raiser->error_code));
        if (raiser->msgbuf->message[0] != '\0')
            fprintf(stderr, ": %s", raiser->msgbuf->message);
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
            if (parser->vm->err_function != NULL) {
                fprintf(stderr, "    Function \"%s\"\n",
                        parser->vm->err_function->trace_name);
            }

            for (i = parser->vm->method_stack_pos-1;i >= 0;i--) {
                entry = vm_stack[i];
                fprintf(stderr, "    Method \"%s\" at line %d.\n",
                        entry->method->trace_name, entry->line_num);
            }
        }
        exit(EXIT_FAILURE);
    }

    lily_free_parse_state(parser);
    exit(EXIT_SUCCESS);
}
