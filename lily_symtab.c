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

void lily_add_storage(lily_symtab *symtab, lily_storage *storage)
{
    lily_storage *new_storage = lily_malloc(sizeof(lily_storage));
    if (new_storage == NULL)
        lily_raise_nomem(symtab->error);

    new_storage->id = symtab->next_storage_id;
    symtab->next_storage_id++;

    new_storage->flags = STORAGE_SYM;
    new_storage->expr_num = 0;
    new_storage->cls = storage->cls;
    new_storage->next = storage->next;
    storage->next = new_storage;
}

lily_class *lily_class_by_id(lily_symtab *symtab, int class_id)
{
    return symtab->classes[class_id];
}

lily_class *lily_class_by_name(lily_symtab *symtab, char *name)
{
    int i;
    lily_class **classes = symtab->classes;

    for (i = 0;i <= SYM_CLASS_FUNCTION;i++) {
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

        if (isafunc(var)) {
            lily_func_prop *fp = var->properties;
            lily_free(fp->code);
            lily_free(fp->args);
            lily_free(fp);
        }

        if (var->id > MAIN_FUNC_ID)
            lily_free(var->name);

        lily_free(var);

        var = temp;
    }

    if (symtab->classes != NULL) {
        int i;
        for (i = 0;i <= SYM_CLASS_FUNCTION;i++) {
            lily_class *cls = symtab->classes[i];
            if (cls != NULL) {
                if (cls->storage != NULL) {
                    lily_storage *store_curr = cls->storage;
                    lily_storage *store_start = store_curr;
                    lily_storage *store_next;
                    do {
                        store_next = store_curr->next;
                        lily_free(store_curr);
                        store_curr = store_next;
                    } while (store_curr != store_start);
                }
                lily_free(cls);
            }
        }
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

    symtab->classes = classes;
    class_count = sizeof(class_seeds) / sizeof(class_seeds[0]);
    ret = 1;

    for (i = 0;i < class_count;i++) {
        lily_class *new_class = lily_malloc(sizeof(lily_class));

        if (new_class != NULL) {
            lily_storage *storage = lily_malloc(sizeof(lily_storage));
            if (storage != NULL) {
                storage->id = symtab->next_storage_id;
                symtab->next_storage_id++;
                storage->flags = STORAGE_SYM;
                storage->expr_num = 0;
                storage->cls = new_class;
                storage->next = storage;
            }
            else
                ret = 0;

            new_class->id = i;
            new_class->name = class_seeds[i];
            new_class->storage = storage;
        }
        else
            ret = 0;

        classes[i] = new_class;
    }

    return ret;
}

static int init_symbols(lily_symtab *symtab)
{
    /* Turn the keywords into symbols. */
    int func_count, i, ret;
    lily_class *func_class;

    func_class = lily_class_by_id(symtab, SYM_CLASS_FUNCTION);
    func_count = sizeof(func_seeds) / sizeof(func_seeds[0]);
    ret = 1;

    for (i = 0;i < func_count;i++) {
        func_entry *seed = func_seeds[i];
        lily_var *new_var = lily_malloc(sizeof(lily_var));

        if (new_var == NULL) {
            ret = 0;
            break;
        }

        lily_func_prop *fp = lily_malloc(sizeof(lily_func_prop));
        if (fp == NULL) {
            lily_free(new_var);
            ret = 0;
            break;
        }

        if (seed->num_args == 0) {
            if (seed->arg_ids[0] == -1) {
                fp->code = lily_malloc(4 * sizeof(int));
                if (fp->code == NULL) {
                    lily_free(fp);
                    lily_free(new_var);
                    ret = 0;
                    break;
                }
                fp->pos = 0;
                fp->len = 4;
            }
            else
                fp->code = NULL;

            fp->args = NULL;
            fp->num_args = 0;
        }
        else {
            fp->args = lily_malloc(sizeof(lily_class *) * seed->num_args);
            if (fp->args == NULL) {
                lily_free(fp);
                lily_free(new_var);
                ret = 0;
                break;
            }
            int j;
            for (j = 0;j < seed->num_args;j++)
                fp->args[j] = lily_class_by_id(symtab, seed->arg_ids[j]);

            fp->num_args = seed->num_args;
            fp->code = NULL;
        }

        fp->func = seed->func;
        new_var->name = seed->name;
        new_var->cls = func_class;
        new_var->line_num = 0;
        new_var->properties = fp;
        add_var(symtab, new_var);
    }

    return ret;
}

lily_symtab *lily_new_symtab(lily_excep_data *excep)
{
    lily_symtab *s = lily_malloc(sizeof(lily_symtab));

    if (s == NULL)
        return NULL;

    s->next_lit_id = 0;
    s->next_var_id = 0;
    s->next_storage_id = 0;
    s->var_start = NULL;
    s->var_top = NULL;
    s->classes = NULL;
    s->lit_start = NULL;
    s->lit_top = NULL;

    if (!init_classes(s) || !init_symbols(s)) {
        /* This will free any symbols added, and the symtab object. */
        lily_free_symtab(s);
        return NULL;
    }

    lily_var *main_func = s->var_top;

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

    var->flags = VAR_SYM | S_IS_NIL;
    var->cls = cls;
    var->properties = NULL;
    var->line_num = *symtab->lex_linenum;

    add_var(symtab, var);
    return var;
}

/* Prepare @lily_main to receive new instructions after a parse step. Debug and
   the vm stay within 'pos', so no need to actually clear the code. */
void lily_reset_main(lily_symtab *symtab)
{
    ((lily_func_prop *)symtab->main->properties)->pos = 0;
}
