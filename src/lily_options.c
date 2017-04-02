#include <stdio.h>

#include "lily_alloc.h"
#include "lily_options.h"

lily_options *lily_new_options(void)
{
    lily_options *opt = lily_malloc(sizeof(*opt));
    opt->argc = 0;
    opt->argv = NULL;
    opt->data = stdout;
    opt->render_func = (lily_render_func) fputs;

    return opt;
}

void lily_free_options(lily_options *opt)
{
    lily_free(opt);
}
