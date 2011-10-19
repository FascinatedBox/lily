#include <string.h>

#include "lily_symtab.h"
#include "lily_impl.h"

static char *classnames[] =
{
    "function",
    "str",
    "integer",
    "number",
};

struct lily_keyword {
    char *name;
    int class_id;
    int num_args;
} keywords[] =
{
    {"print", SYM_CLASS_FUNCTION, 1},
    /* All code outside of functions is stuffed here, and at the end of parsing,
       this function is called. */
    {"", SYM_CLASS_FUNCTION, 0}
};

static void add_symbol(lily_symtab *symtab, lily_symbol *s)
{
    s->id = symtab->new_sym_id;
    symtab->new_sym_id++;

    s->next = NULL;
    /* The symtab is the oldest, for iteration. The symtab_top is the newest,
       for adding new elements. */
    if (symtab->start == NULL)
        /* If no symtab, this is both the oldest and newest. */
        symtab->start = s;
    else
        symtab->top->next = s;

    symtab->top = s;
}

lily_class *lily_class_by_id(lily_symtab *symtab, int class_id)
{
    return symtab->classes[class_id];
}

lily_class *lily_class_by_name(lily_symtab *symtab, char *name)
{
    int i;
    lily_class **classes = symtab->classes;

    for (i = 0;i <= SYM_CLASS_NUMBER;i++) {
        if (strcmp(classes[i]->name, name) == 0)
            return classes[i];
    }

    return NULL;
}

void lily_free_symtab(lily_symtab *symtab)
{
    lily_symbol *sym = symtab->start;
    lily_symbol *temp;

    while (sym != NULL) {
        temp = sym->next;

        if (isafunc(sym) && sym->code_data != NULL) {
            lily_free(((lily_code_data *)sym->code_data)->code);
            lily_free(sym->code_data);
        }

        lily_free(sym->name);
        lily_free(sym);

        sym = temp;
    }

    if (symtab->classes != NULL) {
        int i;
        for (i = 0;i <= SYM_CLASS_NUMBER;i++)
            lily_free(symtab->classes[i]);

        lily_free(symtab->classes);
    }

    lily_free(symtab);
}

static int init_classes(lily_symtab *symtab)
{
    int i, class_count, ret;

    lily_class **classes = lily_malloc(sizeof(lily_class) * 4);
    if (classes == NULL)
        return 0;

    class_count = sizeof(classnames) / sizeof(classnames[0]);

    for (i = 0;i < class_count;i++) {
        lily_class *new_class = lily_malloc(sizeof(lily_class));

        if (new_class == NULL)
            break;

        classes[i] = new_class;
        new_class->name = classnames[i];

        new_class->id = symtab->new_sym_id;
        symtab->new_sym_id++;
    }

    if (i != class_count) {
        for (;i > 0;i--)
            lily_free(classes[i]);

        lily_free(classes);
        ret = 0;
    }
    else {
        symtab->classes = classes;
        ret = 1;
    }
    return ret;
}

static int init_symbols(lily_symtab *symtab)
{
    /* Turn the keywords into symbols. */
    int i, keyword_count, ret;
    lily_class *func_class = lily_class_by_id(symtab, SYM_CLASS_FUNCTION);

    if (symtab->classes == NULL)
        return 0;

    keyword_count = sizeof(keywords) / sizeof(keywords[0]);
    ret = 1;

    for (i = 0;i < keyword_count;i++) {
        lily_symbol *new_sym = lily_malloc(sizeof(lily_symbol));

        if (new_sym == NULL) {
            ret = 0;
            break;
        }

        new_sym->name = lily_malloc(strlen(keywords[i].name) + 1);
        if (new_sym->name == NULL) {
            ret = 0;
            lily_free(new_sym);
            break;
        }

        strcpy(new_sym->name, keywords[i].name);
        new_sym->code_data = NULL;
        new_sym->num_args = keywords[i].num_args;
        new_sym->value = NULL;
        new_sym->sym_class = func_class;
        new_sym->line_num = 0;
        add_symbol(symtab, new_sym);
    }

    return ret;
}

lily_symtab *lily_new_symtab(lily_excep_data *excep)
{
    lily_symtab *s = lily_malloc(sizeof(lily_symtab));

    if (s == NULL)
        return NULL;

    int *code = lily_malloc(sizeof(int) * 4);
    lily_code_data *cd = lily_malloc(sizeof(lily_code_data));

    s->new_sym_id = 0;
    s->start = NULL;
    s->top = NULL;
    s->classes = NULL;

    if (code == NULL || cd == NULL || !init_classes(s) || !init_symbols(s)) {
        /* This will free any symbols added, and the symtab object. */
        lily_free_symtab(s);
        lily_free(cd);
        lily_free(code);
        return NULL;
    }

    cd->code = code;
    cd->len = 4;
    cd->pos = 0;

    lily_symbol *main_func = s->top;

    main_func->code_data = cd;
    s->main = main_func;
    s->error = excep;

    return s;
}

lily_symbol *lily_sym_by_name(lily_symtab *symtab, char *name)
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

lily_symbol *lily_new_var(lily_symtab *symtab, lily_class *cls, char *name)
{
    lily_symbol *sym = lily_malloc(sizeof(lily_symbol));
    if (sym == NULL)
        lily_raise_nomem(symtab->error);

    sym->name = lily_malloc(strlen(name) + 1);
    if (sym->name == NULL) {
        lily_free(sym);
        lily_raise_nomem(symtab->error);
    }

    sym->code_data = NULL;
    sym->sym_class = cls;
    sym->value = NULL;
    sym->line_num = *symtab->lex_linenum;
    strcpy(sym->name, name);

    add_symbol(symtab, sym);
    return sym;
}

lily_symbol *lily_new_temp(lily_symtab *symtab, lily_class *cls,
                           lily_value value)
{
    lily_symbol *sym = lily_malloc(sizeof(lily_symbol));
    if (sym == NULL)
        lily_raise_nomem(symtab->error);

    switch (cls->id) {
        case SYM_CLASS_INTEGER:
            sym->value = &value.integer;
            break;
        case SYM_CLASS_NUMBER:
            sym->value = &value.number;
            break;
        case SYM_CLASS_STR:
            sym->value = value.ptr;
            break;
    }

    sym->code_data = NULL;
    sym->name = NULL;
    sym->sym_class = cls;
    sym->line_num = *symtab->lex_linenum;
    add_symbol(symtab, sym);
    return sym;
}
