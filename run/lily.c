#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lily_alloc.h"
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
          "-t        : Code is between <?lily ... ?> tags.\n"
          "            Everything else is printed to stdout.\n"
          "            By default, everything is treated as code.\n"
          "-s string : The program is a string (end of options).\n"
          "-g number : The number of objects allowed before a gc pass.\n"
          "file      : The program is the given filename.\n", stderr);
    exit(EXIT_FAILURE);
}

int is_file;
int do_tags = 0;
int gc_threshold = 0;
char *to_process = NULL;

static void process_args(int argc, char **argv)
{
    int i;
    for (i = 1;i < argc;i++) {
        char *arg = argv[i];
        if (strcmp("-h", arg) == 0)
            usage();
        else if (strcmp("-t", arg) == 0)
            do_tags = 1;
        else if (strcmp("-g", arg) == 0) {
            i++;
            if (i + 1 == argc)
                usage();

            gc_threshold = atoi(argv[i]);
        }
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

int main(int argc, char **argv)
{
    process_args(argc, argv);
    if (to_process == NULL)
        usage();

    lily_options *options = lily_new_default_options();
    if (gc_threshold != 0)
        options->gc_threshold = gc_threshold;

    options->argc = argc;
    options->argv = argv;

    lily_parse_state *parser = lily_new_parse_state(options);
    lily_lex_mode mode = (do_tags ? lm_tags : lm_no_tags);

    int result;
    if (is_file == 1)
        result = lily_parse_file(parser, mode, to_process);
    else
        result = lily_parse_string(parser, "[cli]", mode, to_process);

    if (result == 0) {
        fputs(lily_build_error_message(parser), stderr);
        exit(EXIT_FAILURE);
    }

    lily_free_parse_state(parser);
    lily_free(options);
    exit(EXIT_SUCCESS);
}
