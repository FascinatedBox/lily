#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lily_api_options.h"
#include "lily_api_embed.h"

/*  lily_main.c
    This is THE main runner for Lily. */

static void usage()
{
    fputs("Usage: lily [option] ...\n"
          "Options:\n"
          "-h             : Print this help and exit.\n"
          "-t             : Code is between <?lily ... ?> tags.\n"
          "                 Everything else is printed to stdout.\n"
          "                 By default, everything is treated as code.\n"
          "-s string      : The program is a string (end of options).\n"
          "-gstart N      : Initial # of objects allowed before a gc sweep.\n"
          "-gmul N        : (# allowed * N) when sweep can't free anything.\n"
          "file           : The program is the given filename.\n", stderr);
    exit(EXIT_FAILURE);
}

int is_file;
int do_tags = 0;
int gc_start = -1;
int gc_multiplier = -1;
char *to_process = NULL;

static void process_args(int argc, char **argv, int *argc_offset)
{
    int i;
    for (i = 1;i < argc;i++) {
        char *arg = argv[i];
        if (strcmp("-h", arg) == 0)
            usage();
        else if (strcmp("-t", arg) == 0)
            do_tags = 1;
        else if (strcmp("-gstart", arg) == 0) {
            i++;
            if (i + 1 == argc)
                usage();

            gc_start = atoi(argv[i]);
        }
        else if (strcmp("-gmul", arg) == 0) {
            i++;
            if (i + 1 == argc)
                usage();

            gc_multiplier = atoi(argv[i]);
        }
        else if (strcmp("-s", arg) == 0) {
            i++;
            if (i == argc)
                usage();

            is_file = 0;
            break;
        }
        else {
            is_file = 1;
            break;
        }
    }

    to_process = argv[i];
    *argc_offset = i;
}

int main(int argc, char **argv)
{
    int argc_offset;
    process_args(argc, argv, &argc_offset);
    if (to_process == NULL)
        usage();

    lily_options *options = lily_new_default_options();
    if (gc_start != -1)
        options->gc_start = gc_start;
    if (gc_multiplier != -1)
        options->gc_multiplier = gc_multiplier;

    options->argc = argc - argc_offset;
    options->argv = argv + argc_offset;

    lily_state *state = lily_new_state(options);

    int result;

    if (do_tags) {
        if (is_file == 1)
            result = lily_exec_template_file(state, to_process);
        else
            result = lily_exec_template_string(state, "[cli]", to_process);
    }
    else {
        if (is_file == 1)
            result = lily_parse_file(state, to_process);
        else
            result = lily_parse_string(state, "[cli]", to_process);
    }

    if (result == 0) {
        fputs(lily_get_error(state), stderr);
        exit(EXIT_FAILURE);
    }

    lily_free_state(state);
    exit(EXIT_SUCCESS);
}
