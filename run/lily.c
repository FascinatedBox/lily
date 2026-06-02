#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lily.h"

extern uint8_t lily_repl(lily_state *);

#define arg_equal(b) strcmp(arg, b) == 0
#define CHECK_NEXT \
i++; \
if (i == argc) \
    missing_value(arg);

static void usage()
{
    fputs(
          "Usage: lily [options] [script [arguments]]\n"
          "\n"
          "Available options are:\n"
          "  -s cmd        execute string 'cmd' instead of a script\n"
          "  -l            local imports only (don't use system dirs)\n"
          "  -gstart N     # of values to allow before a gc sweep\n"
          "  -gmul N       (# allowed * N) when sweep can't free anything\n"
          "  --            stop handling options\n"
          "  -v            show version information\n"
          "  -h            show this help\n"
          , stderr);
    exit(EXIT_FAILURE);
}

static void show_version_and_exit()
{
    puts("Lily v" LILY_MAJOR "." LILY_MINOR);
    exit(EXIT_SUCCESS);
}

static void invalid_arg(const char *opt)
{
    /* Use two lines to match the above using two lines. */
    fprintf(stderr, "lily: %s is not a valid option\n\n", opt);
    usage();
}

static void missing_value(const char *opt)
{
    /* Use two lines to match the above using two lines. */
    fprintf(stderr, "lily: %s requires a value\n\n", opt);
    usage();
}

int is_file;
int gc_start = -1;
int gc_multiplier = -1;
int use_sys_dirs = 1;
char *to_process = NULL;

static void process_args(int argc, char **argv, int *argc_offset)
{
    int i;

    for (i = 1;i < argc;i++) {
        char *arg = argv[i];

        if (arg[0] == '-') {
            if (arg_equal("-h"))
                usage();
            else if (arg_equal("-gstart")) {
                CHECK_NEXT
                gc_start = atoi(argv[i]);
            }
            else if (arg_equal("-gmul")) {
                CHECK_NEXT
                gc_multiplier = atoi(argv[i]);
            }
            else if (arg_equal("-s")) {
                CHECK_NEXT
                is_file = 0;
                break;
            }
            else if (arg_equal("-l"))
                use_sys_dirs = 0;
            else if (arg_equal("--")) {
                /* The repl safely handles i == argc. */
                i++;
                is_file = 1;
                break;
            }
            else if (arg_equal("-v"))
                show_version_and_exit();
            else
                invalid_arg(arg);
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

    lily_config config;

    lily_config_init(&config);

    if (gc_start != -1)
        config.gc_start = gc_start;
    if (gc_multiplier != -1)
        config.gc_multiplier = gc_multiplier;

    config.argc = argc - argc_offset;
    config.argv = argv + argc_offset;
    config.use_sys_dirs = use_sys_dirs;

    lily_state *state = lily_new_state(&config);

    if (to_process == NULL)
        exit(lily_repl(state));

    int result;

    if (is_file == 1)
        result = lily_load_file(state, to_process);
    else
        result = lily_load_string(state, "[cli]", to_process);

    if (result)
        result = lily_parse_content(state);

    if (result == 0)
        fputs(lily_error_message(state), stderr);

    int exit_code = lily_exit_code(state);

    lily_free_state(state);
    exit(exit_code);
}
