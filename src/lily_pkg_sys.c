/**
library sys

The sys package provides access to the arguments given to Lily.
*/

#include <stdlib.h>
#include <string.h>

#include "lily.h"

/** Begin autogen section. **/
const char *lily_sys_table[] = {
    "\0\0"
    ,"F\0getenv\0(String):Option[String]"
    ,"R\0argv\0List[String]"
    ,"Z"
};
#define toplevel_OFFSET 1
void lily_sys__getenv(lily_state *);
void lily_sys_var_argv(lily_state *);
void *lily_sys_loader(lily_state *s, int id)
{
    switch (id) {
        case toplevel_OFFSET + 0: return lily_sys__getenv;
        case toplevel_OFFSET + 1: lily_sys_var_argv(s); return NULL;
        default: return NULL;
    }
}
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
