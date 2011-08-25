#include <string.h>

#include "lily_symtab.h"
#include "lily_impl.h"

struct lily_keyword {
    char *name;
    int callable;
    int num_args;
} keywords[] =
{
    {"str", 0, 0},
    {"print", 1, 1},
    /* All code outside of functions is stuffed here, and at the end of parsing,
       this function is called. */
    {"", 1, 1}
};

static int new_sym_id = 0;

/* Might want to look into merging initializers later. */
static void init_builtin_symbol(lily_symbol *s)
{
    s->code_data = NULL;
    s->val_type = vt_builtin;
    s->sym_value = NULL;
    s->next = symtab;
    symtab = s;
    s->sym_id = new_sym_id;
    new_sym_id++;
}

static void init_temp_symbol(lily_symbol *s)
{
    s->code_data = NULL;
    s->sym_value = NULL;
    s->next = symtab;
    symtab = s;
    s->sym_id = new_sym_id;
    new_sym_id++;
}

void lily_init_symtab(void)
{
    /* Turn keywords into symbols. */
    int i, kw_count;

    kw_count = sizeof(keywords) / sizeof(keywords[0]);
    for (i = 0;i < kw_count;i++) {
        lily_symbol *s = lily_impl_malloc(sizeof(lily_symbol));
        init_builtin_symbol(s);

        s->sym_name = lily_impl_malloc(strlen(keywords[i].name) + 1);

        strcpy(s->sym_name, keywords[i].name);
        s->callable = keywords[i].callable;
        s->num_args = keywords[i].num_args;
    }

    main_func = symtab;

    lily_code_data *cd = lily_impl_malloc(sizeof(lily_code_data));
    cd->code = lily_impl_malloc(sizeof(int) * 4);
    cd->code_len = 4;
    cd->code_pos = 0;

    main_func->code_data = cd;
}

lily_symbol *lily_st_find_symbol(char *name)
{
    lily_symbol *sym;

    sym = symtab;
    while (sym != NULL) {
        if (sym->sym_name != NULL && strcmp(sym->sym_name, name) == 0)
            return sym;
        sym = sym->next;
    }
    return NULL;
}

lily_symbol *lily_st_new_str_sym(char *str_val)
{
    lily_symbol *sym = lily_impl_malloc(sizeof(lily_symbol));
    init_temp_symbol(sym);

    lily_strval *strval = lily_impl_malloc(sizeof(lily_strval));
    int str_size = strlen(str_val);
    char *str = lily_impl_malloc(str_size + 1);

    strcpy(str, str_val);

    strval->str = str;
    strval->str_size = str_size;

    sym->sym_name = NULL;
    sym->val_type = vt_str;
    sym->sym_value = strval;

    return sym;
}

lily_symbol *lily_st_new_int_sym(int int_val)
{
    lily_symbol *sym = lily_impl_malloc(sizeof(lily_symbol));
    init_temp_symbol(sym);

    sym->sym_name = NULL;
    sym->val_type = vt_int;
    sym->sym_value = &int_val;

    return sym;
}

lily_symbol *lily_st_new_dbl_sym(double dbl_val)
{
    lily_symbol *sym = lily_impl_malloc(sizeof(lily_symbol));
    init_temp_symbol(sym);

    sym->sym_name = NULL;
    sym->val_type = vt_double;
    sym->sym_value = &dbl_val;

    return sym;
}

lily_symbol *lily_st_new_var_sym(char *name)
{
    lily_symbol *sym = lily_impl_malloc(sizeof(lily_symbol));
    init_temp_symbol(sym);

    sym->sym_name = lily_impl_malloc(strlen(name) + 1);
    sym->val_type = vt_unknown;
    sym->sym_value = NULL;
    strcpy(sym->sym_name, name);
    return sym;
}
