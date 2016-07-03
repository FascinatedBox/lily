#include <string.h>

#include "lily_parser.h"

#include "lily_api_alloc.h"
#include "lily_api_value_ops.h"
#include "lily_api_options.h"

/**
package sys

The sys package provides access to the arguments given to Lily.
*/

/**
var argv: List[String]

This contains arguments sent to the program through the command-line. If Lily
was not invoked from the command-line (ex: mod_lily), then this is empty.
*/
void *lily_sys_loader(lily_options *options, uint16_t *unused, int id)
{
    lily_value *result = lily_new_empty_value();
    lily_list_val *lv = lily_new_list_val();
    lily_value **values = lily_malloc(options->argc * sizeof(lily_value *));

    lv->elems = values;
    lv->num_values = options->argc;

    int i;
    for (i = 0;i < options->argc;i++)
        values[i] = lily_new_string(options->argv[i]);

    lily_move_list_f(MOVE_DEREF_NO_GC, result, lv);
    return result;
}

const char *sys_table[] =
{
    "\000"
    ,"R\000argv\0List[String]"
    ,"Z"
};

void lily_pkg_sys_init(lily_parse_state *parser, lily_options *options)
{
    lily_register_package(parser, "sys", sys_table, lily_sys_loader);
}
