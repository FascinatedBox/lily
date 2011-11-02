#include <string.h>

#include "lily_impl.h"
#include "lily_symtab.h"

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
    {"@main", SYM_CLASS_FUNCTION, 0}
};

static void add_symbol(lily_symtab *symtab, lily_symbol *s)
{
    s->id = symtab->next_sym_id;
    symtab->next_sym_id++;

    s->next = NULL;
    /* The symtab is the oldest, for iteration. The symtab_top is the newest,
       for adding new elements. */
    if (symtab->sym_start == NULL)
        /* If no symtab, this is both the oldest and newest. */
        symtab->sym_start = s;
    else
        symtab->sym_top->next = s;

    symtab->sym_top = s;
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
    lily_symbol *sym = symtab->sym_start;
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
    int i, class_id, class_count, ret;

    lily_class **classes = lily_malloc(sizeof(lily_class) * 4);
    if (classes == NULL)
        return 0;

    class_id = 0;
    class_count = sizeof(classnames) / sizeof(classnames[0]);

    for (i = 0;i < class_count;i++) {
        lily_class *new_class = lily_malloc(sizeof(lily_class));

        if (new_class == NULL)
            break;

        new_class->name = classnames[i];
        new_class->id = class_id;
        class_id++;

        classes[i] = new_class;
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
        new_sym->object = NULL;
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

    s->next_sym_id = 0;
    s->next_obj_id = 0;
    s->sym_start = NULL;
    s->sym_top = NULL;
    s->classes = NULL;
    s->obj_start = NULL;
    s->obj_top = NULL;

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

    lily_symbol *main_func = s->sym_top;

    main_func->code_data = cd;
    s->main = main_func;
    s->error = excep;

    return s;
}

lily_symbol *lily_sym_by_name(lily_symtab *symtab, char *name)
{
    lily_symbol *sym;

    sym = symtab->sym_start;

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
    lily_object *obj;

    if (sym == NULL)
        lily_raise_nomem(symtab->error);

    sym->name = lily_malloc(strlen(name) + 1);
    if (sym->name == NULL) {
        lily_free(sym);
        lily_raise_nomem(symtab->error);
    }

    obj = lily_malloc(sizeof(lily_object));
    if (obj == NULL) {
        lily_free(sym);
        lily_free(sym->name);
        lily_raise_nomem(symtab->error);
    }
    obj->sym = sym;
    obj->cls = cls;
    obj->flags = OB_SYM;
    sym->object = obj;
    sym->code_data = NULL;
    sym->sym_class = cls;

    sym->line_num = *symtab->lex_linenum;
    strcpy(sym->name, name);

    add_symbol(symtab, sym);
    obj->id = sym->id;
    return sym;
}

lily_object *lily_new_fixed(lily_symtab *symtab, lily_class *cls)
{
    lily_object *o = lily_malloc(sizeof(lily_object));
    if (o == NULL)
        lily_raise_nomem(symtab->error);

    o->cls = cls;
    o->sym = NULL;
    o->flags = OB_FIXED;

    o->next = NULL;
    if (symtab->obj_start == NULL)
        symtab->obj_start = o;
    else
        symtab->obj_top->next = o;

    symtab->obj_top = o;
    o->id = symtab->next_obj_id;
    symtab->next_obj_id++;

    return o;
}
