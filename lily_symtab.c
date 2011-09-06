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

static void add_symbol(lily_interp *itp, lily_symbol *s)
{
    s->sym_id = itp->new_sym_id;
    itp->new_sym_id++;

    s->next = itp->symtab;
    itp->symtab = s;
}

static void init_temp_symbol(lily_symbol *s)
{
    s->code_data = NULL;
    s->sym_value = NULL;
    s->callable = 0;
}

void lily_init_symtab(lily_interp *itp)
{
    /* Turn keywords into symbols. */
    int i, kw_count;

    kw_count = sizeof(keywords) / sizeof(keywords[0]);
    itp->new_sym_id = 0;
    itp->symtab = NULL;

    for (i = 0;i < kw_count;i++) {
        lily_symbol *s = lily_impl_malloc(sizeof(lily_symbol));

        s->sym_name = lily_impl_malloc(strlen(keywords[i].name) + 1);

        strcpy(s->sym_name, keywords[i].name);
        s->code_data = NULL;
        s->callable = keywords[i].callable;
        s->num_args = keywords[i].num_args;
        s->sym_value = NULL;
        s->val_type = vt_builtin;
        add_symbol(itp, s);
    }

    lily_symbol *main_func = itp->symtab;
    lily_code_data *cd = lily_impl_malloc(sizeof(lily_code_data));
    cd->code = lily_impl_malloc(sizeof(int) * 4);
    cd->code_len = 4;
    cd->code_pos = 0;

    main_func->code_data = cd;
    itp->main_func = main_func;
}

lily_symbol *lily_st_find_symbol(lily_symbol *symtab, char *name)
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

lily_symbol *lily_st_new_str_sym(lily_interp *itp, char *str_val)
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

    add_symbol(itp, sym);
    return sym;
}

lily_symbol *lily_st_new_int_sym(lily_interp *itp, int int_val)
{
    lily_symbol *sym = lily_impl_malloc(sizeof(lily_symbol));
    init_temp_symbol(sym);

    sym->sym_name = NULL;
    sym->val_type = vt_int;
    sym->sym_value = &int_val;

    add_symbol(itp, sym);
    return sym;
}

lily_symbol *lily_st_new_dbl_sym(lily_interp *itp, double dbl_val)
{
    lily_symbol *sym = lily_impl_malloc(sizeof(lily_symbol));
    init_temp_symbol(sym);

    sym->sym_name = NULL;
    sym->val_type = vt_double;
    sym->sym_value = &dbl_val;

    add_symbol(itp, sym);
    return sym;
}

lily_symbol *lily_st_new_var_sym(lily_interp *itp, char *name)
{
    lily_symbol *sym = lily_impl_malloc(sizeof(lily_symbol));
    init_temp_symbol(sym);

    sym->sym_name = lily_impl_malloc(strlen(name) + 1);
    sym->val_type = vt_unknown;
    sym->sym_value = NULL;
    strcpy(sym->sym_name, name);

    add_symbol(itp, sym);
    return sym;
}
