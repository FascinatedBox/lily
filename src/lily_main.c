#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lily_parser.h"

/*  lily_main.c
    This is THE main runner for Lily. */

void lily_impl_puts(void *data, char *text)
{
    fputs(text, stdout);
}

static void usage()
{
    fputs("Usage: lily [option] ...\n"
          "Options:\n"
          "-h        : Print this help and exit.\n"
          "-s string : The program is a string (end of options).\n"
          "            By default, does not process tags.\n"
          "file      : The program is the given filename.\n"
          "            Processes tags by default.\n", stderr);
    exit(EXIT_FAILURE);
}

int is_file;
char *to_process = NULL;

static void process_args(int argc, char **argv)
{
    int i;
    for (i = 1;i < argc;i++) {
        char *arg = argv[i];
        if (strcmp("-h", arg) == 0)
            usage();
        else if (strcmp("-s", arg) == 0) {
            i++;
            if (i == argc)
                usage();

            to_process = argv[i];
            if ((i + 1) != argc)
                usage();

            is_file = 0;
            break;
        }
        else {
            to_process = argv[i];
            if ((i + 1) != argc)
                usage();

            is_file = 1;
            break;
        }
    }
}

void traceback_to_file(lily_parse_state *parser, FILE *outfile)
{
    lily_raiser *raiser = parser->raiser;
    fprintf(outfile, "%s", lily_name_for_error(raiser->error_code));
    if (raiser->msgbuf->message[0] != '\0')
        fprintf(outfile, ": %s", raiser->msgbuf->message);
    else
        fputc('\n', outfile);

    if (parser->mode == pm_parse) {
        int line_num;
        if (raiser->line_adjust == 0)
            line_num = parser->lex->line_num;
        else
            line_num = raiser->line_adjust;

        fprintf(outfile, "Where: File \"%s\" at line %d\n",
                parser->lex->filename, line_num);
    }
    else if (parser->mode == pm_execute) {
        lily_vm_stack_entry **vm_stack;
        lily_vm_stack_entry *entry;
        int i;

        vm_stack = parser->vm->function_stack;
        fprintf(outfile, "Traceback:\n");

        for (i = parser->vm->function_stack_pos-1;i >= 0;i--) {
            entry = vm_stack[i];
            char *class_name = entry->function->class_name;
            char *separator;
            if (class_name == NULL) {
                class_name = "";
                separator = "";
            }
            else
                separator = "::";

            if (entry->function->code == NULL)
                fprintf(outfile, "    Function %s%s%s [builtin]\n",
                        class_name, separator,
                        entry->function->trace_name);
            else
                fprintf(outfile, "    Function %s%s%s at line %d\n",
                        class_name, separator,
                        entry->function->trace_name, entry->line_num);
        }
    }
}

int main(int argc, char **argv)
{
    process_args(argc, argv);

    lily_parse_state *parser = lily_new_parse_state(NULL, argc, argv);
    if (parser == NULL) {
        fputs("NoMemoryError: No memory to alloc interpreter.\n", stderr);
        exit(EXIT_FAILURE);
    }

    int result;
    if (is_file == 1)
        result = lily_parse_file(parser, lm_no_tags, to_process);
    else
        result = lily_parse_string(parser, lm_no_tags, to_process);

    if (result == 0) {
        traceback_to_file(parser, stderr);
        exit(EXIT_FAILURE);
    }

    lily_free_parse_state(parser);
    exit(EXIT_SUCCESS);
}
