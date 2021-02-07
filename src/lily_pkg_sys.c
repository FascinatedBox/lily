#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lily.h"
#include "lily_vm.h"
#define LILY_NO_EXPORT
#include "lily_pkg_sys_bindings.h"

extern void lily_parser_exit(lily_state *, uint8_t);

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

void lily_sys__exit(lily_state *s)
{
    lily_parser_exit(s, lily_arg_byte(s, 0));
}

void lily_sys__exit_failure(lily_state *s)
{
    lily_parser_exit(s, EXIT_FAILURE);
}

void lily_sys__exit_success(lily_state *s)
{
    lily_parser_exit(s, EXIT_SUCCESS);
}

void lily_sys__getenv(lily_state *s)
{
    char *env = getenv(lily_arg_string_raw(s, 0));

    if (env) {
        lily_push_string(s, env);
        lily_return_some_of_top(s);
    }
    else
        lily_return_none(s);
}

void lily_sys__recursion_limit(lily_state *s)
{
    lily_return_integer(s, s->depth_max);
}

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

LILY_DECLARE_SYS_CALL_TABLE
