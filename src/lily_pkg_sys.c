/**
library sys

The sys package provides access to the arguments given to Lily.
*/

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lily.h"
#include "lily_vm.h"

/** Begin autogen section. **/
const char *lily_sys_info_table[] = {
    "\0\0"
    ,"F\0getenv\0(String): Option[String]"
    ,"F\0recursion_limit\0: Integer"
    ,"F\0set_recursion_limit\0(Integer)"
    ,"R\0argv\0List[String]"
    ,"Z"
};
void lily_sys__getenv(lily_state *);
void lily_sys__recursion_limit(lily_state *);
void lily_sys__set_recursion_limit(lily_state *);
void lily_sys_var_argv(lily_state *);
lily_call_entry_func lily_sys_call_table[] = {
    NULL,
    lily_sys__getenv,
    lily_sys__recursion_limit,
    lily_sys__set_recursion_limit,
    lily_sys_var_argv,
};
/** End autogen section. **/

/**
var argv: List[String]

This contains arguments sent to the program through the command-line. If Lily
was not invoked from the command-line (ex: mod_lily), then this is empty.
*/
void lily_sys_var_argv(lily_state *s)
{
    lily_config *config = lily_config_get(s);
    int opt_argc = config->argc;
    char **opt_argv = config->argv;
    lily_container_val *lv = lily_push_list(s, opt_argc);

    int i;
    for (i = 0;i < opt_argc;i++) {
        lily_push_string(s, opt_argv[i]);
        lily_con_set_from_stack(s, lv, i);
    }
}

/**
define getenv(name: String): Option[String]

Search the environment for `name`, returning either a `Some` with the contents,
or `None`. Internally, this is a wrapper over C's getenv.
*/
void lily_sys__getenv(lily_state *s)
{
    char *env = getenv(lily_arg_string_raw(s, 0));

    if (env) {
        lily_container_val *variant = lily_push_some(s);
        lily_push_string(s, env);
        lily_con_set_from_stack(s, variant, 0);
        lily_return_top(s);
    }
    else
        lily_return_none(s);
}

/**
define recursion_limit: Integer

Return the current recursion limit.
*/
void lily_sys__recursion_limit(lily_state *s)
{
    lily_return_integer(s, s->depth_max);
}

/**
define set_recursion_limit(limit: Integer)

Attempt to set `limit` as the maximum recursion limit.

# Errors

* `ValueError` if `limit` is lower than the current recursion depth, or an
  unreasonable value (too high or low).
*/
void lily_sys__set_recursion_limit(lily_state *s)
{
    int64_t limit = lily_arg_integer(s, 0);

    if (limit < 1 || limit > INT32_MAX)
        lily_ValueError(s, "Limit value (%ld) is not reasonable.", limit);

    if (limit < s->call_depth)
        lily_ValueError(s,
            "Limit value (%ld) is lower than the current recursion depth.",
            limit);

    s->depth_max = (int32_t)limit;
    lily_return_unit(s);
}
