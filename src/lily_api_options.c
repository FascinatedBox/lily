#include <stdio.h>
#include <stdint.h>

#include "lily_api_alloc.h"
#include "lily_api_options.h"

struct lily_options_ {
    /* If the gc can't free any tags, multiply the # allowed by this value.
       Clamped to 16. */
    uint8_t gc_multiplier;
    /* Should the sys package be registered? */
    uint8_t allow_sys;
    uint16_t pad;
    /* How many tagged values before the gc needs to sweep? */
    uint32_t gc_start;

    int argc;
    /* This is what `sys.argv` makes visible. */
    char **argv;
    /* The html sender gets this as the second argument. */
    void *data;
    /* This is called when tagged data is seen in the lexer. */
    lily_html_sender html_sender;
};

/* This creates an option structure containing "good" default values. This
   struct is read from to initialize different parts of the interpreter. It can
   be adjusted before running the parser, but never during or after. */
lily_options *lily_new_options(void)
{
    lily_options *options = lily_malloc(sizeof(lily_options));
    /* The gc options are totally arbitrary. */
    options->gc_start = 100;
    options->gc_multiplier = 4;
    options->argc = 0;
    options->argv = NULL;

    options->html_sender = (lily_html_sender) fputs;
    options->data = stdout;
    options->allow_sys = 1;

    return options;
}

void lily_op_allow_sys(lily_options *opt, int allow_sys)
{
    opt->allow_sys = allow_sys;
}

void lily_op_argv(lily_options *opt, int opt_argc, char **opt_argv)
{
    opt->argc = opt_argc;
    opt->argv = opt_argv;
}

void lily_op_data(lily_options *opt, void *data)
{
    opt->data = data;
}

void lily_op_gc_multiplier(lily_options *opt, int gc_multiplier)
{
    if (gc_multiplier > 16)
        gc_multiplier = 16;

    opt->gc_multiplier = (uint8_t)gc_multiplier;
}

void lily_op_gc_start(lily_options *opt, int gc_start)
{
    opt->gc_start = (uint16_t)gc_start;
}

void lily_op_html_sender(lily_options *opt, lily_html_sender html_sender)
{
    opt->html_sender = html_sender;
}

int lily_op_get_allow_sys(lily_options *opt) { return opt->allow_sys; }
char **lily_op_get_argv(lily_options *opt, int *argc)
{
    *argc = opt->argc;
    return opt->argv;
}
void *lily_op_get_data(lily_options *opt) { return opt->data; }
int lily_op_get_gc_multiplier(lily_options *opt) { return opt->gc_multiplier; }
int lily_op_get_gc_start(lily_options *opt) { return opt->gc_start; }
lily_html_sender lily_op_get_html_sender(lily_options *opt) { return opt->html_sender; }

void lily_free_options(lily_options *o)
{
    lily_free(o);
}
