#include <string.h>

#include "lily_impl.h"
#include "lily_symtab.h"

static void bind_strlist(lily_symtab *symtab, int strlist_size, char **strlist, int *ok)
{
    *ok = 0;

    const int ids[] = {SYM_CLASS_LIST, SYM_CLASS_STR};

    lily_sig *list_str_sig = lily_try_sig_from_ids(symtab, ids);
    if (list_str_sig == NULL)
        return;

    lily_sig *str_sig = list_str_sig->siglist[0];
    lily_var *bound_var = lily_try_new_var(symtab, list_str_sig, "argv",
        1986490977, 0);
    if (bound_var == NULL)
        return;

    lily_list_val *lv = lily_malloc(sizeof(lily_list_val));
    lily_value **values = lily_malloc(strlist_size * sizeof(lily_value));
    if (lv == NULL || values == NULL) {
        lily_free(lv);
        lily_free(values);
        return;
    }

    lv->gc_entry = NULL;
    lv->elems = values;
    lv->num_values = strlist_size;
    lv->refcount = 1;
    lv->elems = values;
    bound_var->value.list = lv;
    bound_var->flags &= ~VAL_IS_NIL;

    int i, err;
    err = 0;
    for (i = 0;i < strlist_size;i++) {
        values[i] = lily_malloc(sizeof(lily_value));
        if (values[i] == NULL) {
            lv->num_values = i - 1;
            err = 1;
            break;
        }
        values[i]->sig = str_sig;
        values[i]->flags = VAL_IS_NIL;
    }

    if (err == 0) {
        for (i = 0;i < strlist_size;i++) {
            lily_str_val *sv = lily_malloc(sizeof(lily_str_val));
            char *raw_str = lily_malloc(strlen(strlist[i]) + 1);
            lily_value *v = lily_malloc(sizeof(lily_value));

            if (sv == NULL || raw_str == NULL || v == NULL) {
                lily_free(sv);
                lily_free(raw_str);
                lily_free(v);
            }
            strcpy(raw_str, strlist[i]);
            sv->size = strlen(strlist[i]);
            sv->refcount = 1;
            sv->str = raw_str;
            values[i]->flags = 0;
	        values[i]->value.str = sv;
        }
    }

    *ok = 1;
}

int lily_pkg_sys_init(lily_symtab *symtab, int argc, char **argv)
{
    if (symtab == NULL)
        return 0;

    int ok;
    bind_strlist(symtab, argc, argv, &ok);

    return ok;
}