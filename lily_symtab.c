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
    {"", 0, 0}
};

static void add_symbol(lily_symtab *symtab, lily_symbol *s)
{
    s->id = symtab->new_sym_id;
    symtab->new_sym_id++;

    s->next = NULL;
    /* The symtab is the oldest, for iteration. The symtab_top is the newest,
       for adding new elements. */
    if (symtab->start == NULL) {
        /* If no symtab, this is both the oldest and newest. */
        symtab->start = s;
        symtab->top = s;
    }
    else {
        symtab->top->next = s;
        symtab->top = s;
    }
}

void lily_free_symtab(lily_symtab *symtab)
{
    lily_symbol *sym = symtab->start;
    lily_symbol *temp;

    while (sym != NULL) {
        temp = sym->next;

        if (sym->callable && sym->code_data != NULL) {
            lily_free(((lily_code_data *)sym->code_data)->code);
            lily_free(sym->code_data);
        }

        lily_free(sym->name);
        lily_free(sym);

        sym = temp;
    }

    lily_free(symtab);
}

lily_symtab *lily_new_symtab(lily_excep_data *excep)
{
    /* Turn keywords into symbols. */
    int i, kw_count;
    int *code = lily_malloc(sizeof(int) * 4);
    lily_code_data *cd = lily_malloc(sizeof(lily_code_data));
    lily_symtab *s = lily_malloc(sizeof(lily_symtab));

    if (s == NULL || cd == NULL || code == NULL) {
        lily_free(s);
        lily_free(cd);
        lily_free(code);
        return NULL;
    }

    kw_count = sizeof(keywords) / sizeof(keywords[0]);
    s->new_sym_id = 0;
    s->start = NULL;
    s->top = NULL;
    s->error = excep;

    cd->code = code;
    cd->len = 4;
    cd->pos = 0;

    for (i = 0;i < kw_count;i++) {
        lily_symbol *new_sym = lily_malloc(sizeof(lily_symbol));

        if (new_sym == NULL)
            break;

        new_sym->name = lily_malloc(strlen(keywords[i].name) + 1);
        if (new_sym->name == NULL) {
            lily_free(new_sym);
            break;
        }

        strcpy(new_sym->name, keywords[i].name);
        new_sym->code_data = NULL;
        new_sym->callable = keywords[i].callable;
        new_sym->num_args = keywords[i].num_args;
        new_sym->value = NULL;
        new_sym->val_type = vt_builtin;
        new_sym->line_num = 0;
        add_symbol(s, new_sym);
    }

    if (i != kw_count) {
        lily_free_symtab(s);
        /* The main func was never inserted into the symtab, so the code that
           would have gone into it must be manually free'd. */
        lily_free(code);
        lily_free(cd);
        return NULL;
    }

    lily_symbol *main_func = s->top;

    main_func->code_data = cd;
    s->main = main_func;

    return s;
}

static void init_temp_symbol(lily_symbol *s)
{
    s->code_data = NULL;
    s->value = NULL;
    s->callable = 0;
}

lily_symbol *lily_st_find_symbol(lily_symtab *symtab, char *name)
{
    lily_symbol *sym;

    sym = symtab->start;

    while (sym != NULL) {
        if (sym->name != NULL && strcmp(sym->name, name) == 0)
            return sym;
        sym = sym->next;
    }
    return NULL;
}

lily_symbol *lily_st_new_str_sym(lily_symtab *symtab, char *str_val)
{
    lily_symbol *sym = lily_malloc(sizeof(lily_symbol));
    init_temp_symbol(sym);

    lily_strval *strval = lily_malloc(sizeof(lily_strval));
    int str_size = strlen(str_val);
    char *str = lily_malloc(str_size + 1);

    strcpy(str, str_val);

    strval->str = str;
    strval->size = str_size;

    sym->name = NULL;
    sym->val_type = vt_str;
    sym->value = strval;
    sym->line_num = *symtab->lex_linenum;

    add_symbol(symtab, sym);
    return sym;
}

lily_symbol *lily_st_new_int_sym(lily_symtab *symtab, int int_val)
{
    lily_symbol *sym = lily_malloc(sizeof(lily_symbol));
    init_temp_symbol(sym);

    sym->name = NULL;
    sym->val_type = vt_int;
    sym->value = &int_val;
    sym->line_num = *symtab->lex_linenum;

    add_symbol(symtab, sym);
    return sym;
}

lily_symbol *lily_st_new_dbl_sym(lily_symtab *symtab, double dbl_val)
{
    lily_symbol *sym = lily_malloc(sizeof(lily_symbol));
    init_temp_symbol(sym);

    sym->name = NULL;
    sym->val_type = vt_double;
    sym->value = &dbl_val;
    sym->line_num = *symtab->lex_linenum;

    add_symbol(symtab, sym);
    return sym;
}

lily_symbol *lily_st_new_var_sym(lily_symtab *symtab, char *name)
{
    lily_symbol *sym = lily_malloc(sizeof(lily_symbol));
    init_temp_symbol(sym);

    sym->name = lily_malloc(strlen(name) + 1);
    sym->val_type = vt_unknown;
    sym->value = NULL;
    sym->line_num = *symtab->lex_linenum;
    strcpy(sym->name, name);

    add_symbol(symtab, sym);
    return sym;
}
