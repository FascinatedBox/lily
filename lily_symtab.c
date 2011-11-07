#include <string.h>

#include "lily_impl.h"
#include "lily_symtab.h"

/* This creates the *_seed values and defines MAIN_FUNC_ID. */
#include "lily_seed_symtab.h"

static void add_var(lily_symtab *symtab, lily_var *s)
{
    s->id = symtab->next_var_id;
    symtab->next_var_id++;

    s->next = NULL;
    /* The symtab is the oldest, for iteration. The symtab_top is the newest,
       for adding new elements. */
    if (symtab->var_start == NULL)
        /* If no symtab, this is both the oldest and newest. */
        symtab->var_start = s;
    else
        symtab->var_top->next = s;

    symtab->var_top = s;
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
    lily_var *var = symtab->var_start;
    lily_var *temp;

    while (var != NULL) {
        temp = var->next;

        if (isafunc(var) && var->code_data != NULL) {
            lily_free(((lily_code_data *)var->code_data)->code);
            lily_free(var->code_data);
        }
        if (var->id > MAIN_FUNC_ID)
            lily_free(var->name);

        lily_free(var);

        var = temp;
    }

    if (symtab->classes != NULL) {
        int i;
        for (i = 0;i <= SYM_CLASS_NUMBER;i++) {
            lily_method *curr = symtab->classes[i]->methods;
            lily_method *next;
            while (curr) {
                next = curr->next;
                lily_free(curr);
                curr = next;
            }
            lily_free(symtab->classes[i]);
        }
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

    symtab->classes = classes;
    class_id = 0;
    class_count = sizeof(class_seeds) / sizeof(class_seeds[0]);
    ret = 1;

    for (i = 0;i < class_count;i++) {
        lily_class *new_class = lily_malloc(sizeof(lily_class));

        if (new_class != NULL) {
            new_class->name = class_seeds[i].name;
            new_class->id = class_id;
            new_class->methods = NULL;
            if (class_seeds[i].methods != NULL) {
                const method_seed *seed_method = class_seeds[i].methods;
                lily_method *new_method;
                do {
                    new_method = lily_malloc(sizeof(lily_method));
                    if (new_method == NULL) {
                        ret = 0;
                        break;
                    }

                    new_method->method_op = seed_method->method_op;
                    new_method->next = new_class->methods;
                    new_method->rhs = lily_class_by_id(symtab,
                                            seed_method->rhs_id);
                    new_method->result = lily_class_by_id(symtab,
                                            seed_method->result_id);
                    new_class->methods = new_method;
                    seed_method = seed_method->next;
                } while (seed_method != NULL);
            }

            class_id++;
        }
        else {
            new_class->methods = NULL;
            ret = 0;
        }

        classes[i] = new_class;
    }

    return ret;
}

static int init_symbols(lily_symtab *symtab)
{
    /* Turn the keywords into symbols. */
    int i, var_count, ret;
    lily_class *func_class;

    func_class = lily_class_by_id(symtab, SYM_CLASS_FUNCTION);
    if (symtab->classes == NULL)
        return 0;

    var_count = sizeof(var_seeds) / sizeof(var_seeds[0]);
    ret = 1;

    for (i = 0;i < var_count;i++) {
        lily_var *new_var = lily_malloc(sizeof(lily_var));

        if (new_var == NULL) {
            ret = 0;
            break;
        }

        new_var->name = var_seeds[i].name;
        new_var->code_data = NULL;
        new_var->num_args = var_seeds[i].num_args;
        new_var->cls = func_class;
        new_var->line_num = 0;
        add_var(symtab, new_var);
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

    s->next_lit_id = 0;
    s->next_var_id = 0;
    s->var_start = NULL;
    s->var_top = NULL;
    s->classes = NULL;
    s->lit_start = NULL;
    s->lit_top = NULL;

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

    lily_var *main_func = s->var_top;

    main_func->code_data = cd;
    s->main = main_func;
    s->error = excep;

    return s;
}

lily_var *lily_var_by_name(lily_symtab *symtab, char *name)
{
    lily_var *var = symtab->var_start;

    while (var != NULL) {
        if (var->name != NULL && strcmp(var->name, name) == 0)
            return var;
        var = var->next;
    }
    return NULL;
}

lily_literal *lily_new_literal(lily_symtab *symtab, lily_class *cls)
{
    lily_literal *lit = lily_malloc(sizeof(lily_literal));
    if (lit == NULL)
        lily_raise_nomem(symtab->error);

    lit->cls = cls;
    lit->flags = LITERAL_SYM;
    lit->next = NULL;

    if (symtab->lit_start == NULL)
        symtab->lit_start = lit;
    else
        symtab->lit_top->next = lit;

    symtab->lit_top = lit;
    lit->id = symtab->next_lit_id;
    symtab->next_lit_id++;

    return lit;
}

lily_var *lily_new_var(lily_symtab *symtab, lily_class *cls, char *name)
{
    lily_var *var = lily_malloc(sizeof(lily_var));

    if (var == NULL)
        lily_raise_nomem(symtab->error);

    var->name = lily_malloc(strlen(name) + 1);
    if (var->name == NULL) {
        lily_free(var);
        lily_raise_nomem(symtab->error);
    }

    strcpy(var->name, name);

    var->flags = VAR_SYM;
    var->code_data = NULL;
    var->cls = cls;
    var->line_num = *symtab->lex_linenum;

    add_var(symtab, var);
    return var;
}
