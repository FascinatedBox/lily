#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lily.h"
#include "lily_vm.h"
#define LILY_NO_EXPORT
#include "lily_pkg_sys_bindings.h"

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
