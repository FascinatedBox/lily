#include <string.h>

#include "lily_parser.h"

#define malloc_mem(size)             parser->mem_func(NULL, size)
#define free_mem(ptr)          (void)parser->mem_func(ptr, 0)

void lily_pkg_sys_init(lily_parse_state *parser, int argc, char **argv)
{
    lily_begin_package(parser, "sys");


    lily_symtab *symtab = parser->symtab;
    lily_type *list_string_type = lily_type_by_name(parser, "list[string]");
    lily_type *string_type = list_string_type->subtypes[0];
    lily_var *bound_var = lily_new_var(symtab, list_string_type, "argv", 0);

    lily_list_val *lv = malloc_mem(sizeof(lily_list_val));
    lily_value **values = malloc_mem(argc * sizeof(lily_value));

    lv->gc_entry = NULL;
    lv->elems = values;
    lv->num_values = argc;
    lv->refcount = 1;
    lv->elems = values;
    lv->visited = 0;

    int i;
    for (i = 0;i < argc;i++) {
        values[i] = malloc_mem(sizeof(lily_value));
        values[i]->type = string_type;
        values[i]->flags = VAL_IS_NIL;
    }

    for (i = 0;i < argc;i++) {
        lily_string_val *sv = malloc_mem(sizeof(lily_string_val));
        char *raw_string = malloc_mem(strlen(argv[i]) + 1);

        strcpy(raw_string, argv[i]);
        sv->size = strlen(argv[i]);
        sv->refcount = 1;
        sv->string = raw_string;
        values[i]->flags = 0;
        values[i]->value.string = sv;
    }

    lily_value v;
    v.type = bound_var->type;
    v.flags = 0;
    v.value.list = lv;

    lily_tie_value(symtab, bound_var, &v);


    lily_end_package(parser);
}
