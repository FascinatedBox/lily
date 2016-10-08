#include <string.h>

#include "lily_move.h"

#include "lily_api_alloc.h"
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
static void *load_var_argv(lily_options *options, uint16_t *unused)
{
    lily_list_val *lv = lily_new_list_val_n(options->argc);

    int i;
    for (i = 0;i < options->argc;i++)
        lily_list_set_string(lv, i, lily_new_raw_string(options->argv[i]));

    return lily_new_value_of_list(lv);
}

/**
define getenv(name: String): Option[String]

Search the environment for 'name', returning either a `Some` with the contents,
or `None`. Internally, this is a wrapper over C's getenv.
*/
static void lily_sys_getenv(lily_state *s)
{
    char *env = getenv(lily_arg_string_raw(s, 0));

    if (env) {
        lily_instance_val *variant = lily_new_some();
        lily_variant_set_string(variant, 0, lily_new_raw_string(env));
        lily_return_filled_variant(s, variant);
    }
    else
        lily_return_empty_variant(s, lily_get_none(s));
}

#include "dyna_sys.h"

void lily_pkg_sys_init(lily_state *s, lily_options *options)
{
    register_sys(s);
}
