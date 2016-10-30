#include <string.h>
#include <stdio.h>

#include "lily_api_alloc.h"
#include "lily_api_options.h"

/* This creates an option structure containing "good" default values. This
   struct is read from to initialize different parts of the interpreter. It can
   be adjusted before running the parser, but never during or after. */
lily_options *lily_new_default_options(void)
{
    lily_options *options = lily_malloc(sizeof(lily_options));
    options->version = 1;
    /* The gc options are totally arbitrary. */
    options->gc_start = 100;
    options->gc_multiplier = 4;
    options->argc = 0;
    options->argv = NULL;

    options->html_sender = (lily_html_sender) fputs;
    options->data = stdout;
    options->allow_sys = 1;
    options->free_options = 1;

    return options;
}

void lily_free_options(lily_options *o)
{
    lily_free(o);
}
