#include <string.h>

#include "lily_alloc.h"
#include "lily_parser.h"
#include "lily_seed.h"
#include "lily_cls_list.h"
#include "lily_value.h"

void lily_sys_var_loader(lily_parse_state *parser, lily_var *var)
{
    lily_options *options = parser->options;
    lily_symtab *symtab = parser->symtab;

    lily_list_val *lv = lily_new_list_val();
    lily_value **values = lily_malloc(options->argc * sizeof(lily_value *));

    lv->elems = values;
    lv->num_values = options->argc;

    int i;
    for (i = 0;i < options->argc;i++) {
        lily_string_val *sv = lily_malloc(sizeof(lily_string_val));
        char *raw_string = lily_malloc(strlen(options->argv[i]) + 1);

        strcpy(raw_string, options->argv[i]);
        sv->size = strlen(options->argv[i]);
        sv->refcount = 1;
        sv->string = raw_string;
        values[i] = lily_new_string(sv);
    }

    lily_value v;
    v.flags = VAL_IS_LIST;
    v.value.list = lv;

    lily_tie_value(symtab, var, &v);
}

static const lily_var_seed argv_seed = {NULL, "argv", dyna_var, "list[string]"};

void lily_pkg_sys_init(lily_parse_state *parser, lily_options *options)
{
    lily_register_import(parser, "sys", &argv_seed, lily_sys_var_loader);
}
