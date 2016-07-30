#include <string.h>

#include "lily_move.h"

#include "lily_api_alloc.h"
#include "lily_api_embed.h"
#include "lily_api_value.h"
#include "lily_api_value_flags.h"

/**
embedded sys

The sys package provides access to the arguments given to Lily.
*/

/**
var argv: List[String]

This contains arguments sent to the program through the command-line. If Lily
was not invoked from the command-line (ex: mod_lily), then this is empty.
*/
static void *load_var_argv(lily_options *options, uint16_t *unused)
{
    lily_list_val *lv = lily_new_list_val_n(options->argc);

    int i;
    for (i = 0;i < options->argc;i++)
        lily_list_set_string(lv, i, lily_new_raw_string(options->argv[i]));

    return lily_new_value_of_list(lv);
}

#include "dyna_sys.h"

void lily_pkg_sys_init(lily_state *parser, lily_options *options)
{
    register_sys(parser);
}
