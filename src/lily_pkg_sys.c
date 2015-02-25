#include <string.h>

#include "lily_impl.h"
#include "lily_symtab.h"

#define malloc_mem(size)             symtab->mem_func(NULL, size)
#define free_mem(ptr)          (void)symtab->mem_func(ptr, 0)

static void bind_stringlist(lily_symtab *symtab, int stringlist_size,
        char **stringlist)
{
    const int ids[] = {SYM_CLASS_LIST, SYM_CLASS_STRING};

    lily_type *list_string_type = lily_type_from_ids(symtab, ids);
    lily_type *string_type = list_string_type->subtypes[0];
    lily_var *bound_var = lily_new_var(symtab, list_string_type, "argv", 0);

    lily_list_val *lv = malloc_mem(sizeof(lily_list_val));
    lily_value **values = malloc_mem(stringlist_size * sizeof(lily_value));

    lv->gc_entry = NULL;
    lv->elems = values;
    lv->num_values = stringlist_size;
    lv->refcount = 1;
    lv->elems = values;
    lv->visited = 0;
    bound_var->value.list = lv;
    bound_var->flags &= ~VAL_IS_NIL;

    int i;
    for (i = 0;i < stringlist_size;i++) {
        values[i] = malloc_mem(sizeof(lily_value));
        values[i]->type = string_type;
        values[i]->flags = VAL_IS_NIL;
    }

    for (i = 0;i < stringlist_size;i++) {
        lily_string_val *sv = malloc_mem(sizeof(lily_string_val));
        char *raw_string = malloc_mem(strlen(stringlist[i]) + 1);

        strcpy(raw_string, stringlist[i]);
        sv->size = strlen(stringlist[i]);
        sv->refcount = 1;
        sv->string = raw_string;
        values[i]->flags = 0;
        values[i]->value.string = sv;
    }
}

void lily_pkg_sys_init(lily_symtab *symtab, int argc, char **argv)
{
    lily_class *package_cls = lily_class_by_id(symtab, SYM_CLASS_PACKAGE);
    lily_type *package_type = lily_type_for_class(symtab, package_cls);
    lily_var *bound_var = lily_new_var(symtab, package_type, "sys", 0);

    if (bound_var) {
        lily_var *save_chain = symtab->var_chain;
        int save_spot = symtab->next_register_spot;

        bind_stringlist(symtab, argc, argv);

        lily_package_val *pval = malloc_mem(sizeof(lily_package_val));
        lily_var **package_vars = malloc_mem(1 * sizeof(lily_var *));
        int i = 0;
        lily_var *var_iter = symtab->var_chain;
        while (var_iter != save_chain) {
            package_vars[i] = var_iter;
            i++;
            var_iter = var_iter->next;
        }
        symtab->var_chain = save_chain;
        symtab->next_register_spot = save_spot;

        pval->refcount = 1;
        pval->name = bound_var->name;
        pval->gc_entry = NULL;
        pval->var_count = i;
        pval->vars = package_vars;
        bound_var->flags &= ~VAL_IS_NIL;
        bound_var->value.package = pval;
    }
}