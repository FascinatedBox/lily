#include <stdio.h>
#include <stdint.h>

#include "lily_alloc.h"

#include "lily_api_options.h"

struct lily_options_ {
    /* If the gc can't free any tags, multiply the # allowed by this value.
       Clamped to 16. */
    uint8_t gc_multiplier;
    /* Should the sys package be registered? */
    uint8_t allow_sys;
    /* Parser freezes the options it takes to keep other parts from modifying
       them during execution. */
    uint16_t frozen;
    /* How many tagged values before the gc needs to sweep? */
    uint32_t gc_start;

    int argc;
    /* This is what `sys.argv` makes visible. */
    char **argv;
    /* The html sender gets this as the second argument. */
    void *data;
    /* This is called by lexer when content is seen in template mode. */
    lily_render_func render_func;
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
    options->frozen = 0;

    options->render_func = (lily_render_func) fputs;
    options->data = stdout;
    options->allow_sys = 1;

    return options;
}

void lily_op_allow_sys(lily_options *opt, int allow_sys)
{
    if (opt->frozen)
        return;

    opt->allow_sys = allow_sys;
}

void lily_op_argv(lily_options *opt, int opt_argc, char **opt_argv)
{
    if (opt->frozen)
        return;

    opt->argc = opt_argc;
    opt->argv = opt_argv;
}

void lily_op_data(lily_options *opt, void *data)
{
    if (opt->frozen)
        return;

    opt->data = data;
}

void lily_op_freeze(lily_options *opt)
{
    opt->frozen = 1;
}

void lily_op_gc_multiplier(lily_options *opt, int gc_multiplier)
{
    if (opt->frozen)
        return;

    if (gc_multiplier > 16)
        gc_multiplier = 16;

    opt->gc_multiplier = (uint8_t)gc_multiplier;
}

void lily_op_gc_start(lily_options *opt, int gc_start)
{
    if (opt->frozen)
        return;

    opt->gc_start = (uint16_t)gc_start;
}

void lily_op_render_func(lily_options *opt, lily_render_func render_func)
{
    if (opt->frozen)
        return;

    opt->render_func = render_func;
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
lily_render_func lily_op_get_render_func(lily_options *opt) { return opt->render_func; }

void lily_free_options(lily_options *o)
{
    lily_free(o);
}
