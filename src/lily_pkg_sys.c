#include <string.h>

#include "lily_parser.h"

#include "lily_api_alloc.h"
#include "lily_api_dynaload.h"
#include "lily_api_value_ops.h"
#include "lily_api_options.h"

void lily_sys_var_loader(lily_parse_state *parser, const char *name,
        lily_foreign_tie *tie)
{
    lily_options *options = parser->options;

    lily_list_val *lv = lily_new_list_val();
    lily_value **values = lily_malloc(options->argc * sizeof(lily_value *));

    lv->elems = values;
    lv->num_values = options->argc;

    int i;
    for (i = 0;i < options->argc;i++)
        values[i] = lily_new_string(options->argv[i]);

    lily_move_list_f(MOVE_DEREF_NO_GC, &tie->data, lv);
}

static const lily_var_seed argv_seed = {NULL, "argv", dyna_var, "List[String]"};

void lily_pkg_sys_init(lily_parse_state *parser, lily_options *options)
{
    lily_register_package(parser, "sys", &argv_seed, lily_sys_var_loader);
}
