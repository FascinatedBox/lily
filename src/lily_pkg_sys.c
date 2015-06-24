#include <string.h>

#include "lily_alloc.h"
#include "lily_parser.h"
#include "lily_seed.h"

void lily_sys_var_loader(lily_parse_state *parser, lily_var *var)
{
    lily_options *options = parser->options;
    lily_symtab *symtab = parser->symtab;
    lily_type *string_type = var->type->subtypes[0];

    lily_list_val *lv = lily_malloc(sizeof(lily_list_val));
    lily_value **values = lily_malloc(options->argc * sizeof(lily_value));

    lv->gc_entry = NULL;
    lv->elems = values;
    lv->num_values = options->argc;
    lv->refcount = 1;
    lv->elems = values;
    lv->visited = 0;

    int i;
    for (i = 0;i < options->argc;i++) {
        values[i] = lily_malloc(sizeof(lily_value));
        values[i]->type = string_type;
        values[i]->flags = VAL_IS_NIL;
    }

    for (i = 0;i < options->argc;i++) {
        lily_string_val *sv = lily_malloc(sizeof(lily_string_val));
        char *raw_string = lily_malloc(strlen(options->argv[i]) + 1);

        strcpy(raw_string, options->argv[i]);
        sv->size = strlen(options->argv[i]);
        sv->refcount = 1;
        sv->string = raw_string;
        values[i]->flags = 0;
        values[i]->value.string = sv;
    }

    lily_value v;
    v.type = var->type;
    v.flags = 0;
    v.value.list = lv;

    lily_tie_value(symtab, var, &v);
}

static const lily_var_seed argv_seed = {NULL, "argv", dyna_var, "list[string]"};

void lily_pkg_sys_init(lily_parse_state *parser, lily_options *options)
{
    lily_register_import(parser, "sys", &argv_seed, lily_sys_var_loader);
}
