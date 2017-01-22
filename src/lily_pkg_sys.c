#include <string.h>

#include "lily_move.h"
#include "lily_alloc.h"
#include "lily_api_embed.h"
#include "lily_api_value.h"

/**
embedded sys

The sys package provides access to the arguments given to Lily.
*/

/**
var argv: List[String]

This contains arguments sent to the program through the command-line. If Lily
was not invoked from the command-line (ex: mod_lily), then this is empty.
*/
static void load_var_argv(lily_state *s)
{
    lily_options *options = lily_get_options(s);
    int opt_argc;
    char **opt_argv = lily_op_get_argv(options, &opt_argc);
    lily_container_val *lv = lily_new_list(opt_argc);

    int i;
    for (i = 0;i < opt_argc;i++)
        lily_nth_set(lv, i, lily_box_string(s, lily_new_string(opt_argv[i])));

    lily_push_list(s, lv);
}

/**
define getenv(name: String): Option[String]

Search the environment for `name`, returning either a `Some` with the contents,
or `None`. Internally, this is a wrapper over C's getenv.
*/
static void lily_sys_getenv(lily_state *s)
{
    char *env = getenv(lily_arg_string_raw(s, 0));

    if (env) {
        lily_container_val *variant = lily_new_some();
        lily_nth_set(variant, 0, lily_box_string(s, lily_new_string(env)));
        lily_return_variant(s, variant);
    }
    else
        lily_return_none(s);
}

#include "dyna_sys.h"

void lily_pkg_sys_init(lily_state *s, lily_options *options)
{
    register_sys(s);
}
