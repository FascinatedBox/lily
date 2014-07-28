#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lily_parser.h"

/* fs_main.c :
   fs stands for 'fake server'. This is designed to simulate Lily being run from
   a server. This is considered the 'default' or 'normal' Lily runner. */

void lily_impl_puts(void *data, char *text)
{
    fputs(text, stdout);
}

static void usage()
{
    fputs("usage: lily_fs <filename>\n", stderr);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    if (argc != 2)
        usage();

    char *fs_filename = argv[1];

    lily_parse_state *parser = lily_new_parse_state(NULL, argc, argv);
    if (parser == NULL) {
        fputs("ErrNoMemory: No memory to alloc interpreter.\n", stderr);
        exit(EXIT_FAILURE);
    }

    if (lily_parse_file(parser, lm_tags, fs_filename) == 0) {
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
                if (entry->method)
                    fprintf(stderr, "    Method \"%s\" at line %d.\n",
                            entry->method->trace_name, entry->line_num);
                else {
                    char *class_name = entry->function->class_name;
                    char *separator;
                    if (class_name == NULL) {
                        class_name = "";
                        separator = "";
                    }
                    else
                        separator = "::";

                    fprintf(stderr, "    Function \"%s%s%s\".\n",
                            class_name, separator,
                            entry->function->trace_name);
                }
            }
        }
        exit(EXIT_FAILURE);
    }

    lily_free_parse_state(parser);
    exit(EXIT_SUCCESS);
}
