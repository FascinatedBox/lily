#include <string.h>

#include "lily_impl.h"
#include "lily_symtab.h"

/* This creates the *_seed values. */
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

static void init_func_sig_args(lily_symtab *symtab, lily_func_sig *func_sig,
                               func_entry *entry)
{
    int i;

    func_sig->args = lily_malloc(sizeof(lily_sig *) * entry->num_args);
    if (func_sig->args == NULL)
        return;

    for (i = 0;i < entry->num_args;i++) {
        lily_class *cls = lily_class_by_id(symtab, entry->arg_ids[i]);
        func_sig->args[i] = cls->sig;
    }

    func_sig->ret = NULL;
    func_sig->num_args = entry->num_args;
    func_sig->is_varargs = 0;
}

/* All other signatures that vars use are copies of one held by a class. Those
   will be free'd with the class. */
static void free_var_func_sig(lily_sig *sig)
{
    lily_func_sig *func_sig = sig->node.func;
    lily_free(func_sig->args);
    lily_free(func_sig);
    lily_free(sig);
}

static lily_sig *try_sig_for_class(lily_class *cls)
{
    lily_sig *sig;
    if (cls->id == SYM_CLASS_OBJECT) {
        sig = lily_malloc(sizeof(lily_sig));
        if (sig != NULL) {
            sig->cls = cls;
            sig->node.value_sig = NULL;
        }
    }
    else
        sig = cls->sig;

    return sig;
}

int lily_try_add_storage(lily_symtab *symtab, lily_class *cls)
{
    lily_storage *storage = lily_malloc(sizeof(lily_storage));
    if (storage == NULL)
        return 0;

    lily_sig *sig = try_sig_for_class(cls);
    if (sig == NULL) {
        lily_free(storage);
        return 0;
    }

    storage->id = symtab->next_storage_id;
    symtab->next_storage_id++;
    storage->flags = STORAGE_SYM;
    storage->expr_num = 0;
    storage->sig = sig;

    /* Storages are circularly linked so it's easier to find them. */
    if (cls->storage == NULL) {
        cls->storage = storage;
        cls->storage->next = cls->storage;
    }
    else {
        storage->next = cls->storage->next;
        cls->storage->next = storage;
    }

    return 1;
}

lily_class *lily_class_by_id(lily_symtab *symtab, int class_id)
{
    return symtab->classes[class_id];
}

lily_class *lily_class_by_name(lily_symtab *symtab, char *name)
{
    int i;
    lily_class **classes = symtab->classes;

    for (i = 0;i <= SYM_LAST_CLASS;i++) {
        if (strcmp(classes[i]->name, name) == 0)
            return classes[i];
    }

    return NULL;
}

void lily_free_symtab(lily_symtab *symtab)
{
    int cls_id;
    lily_literal *lit, *lit_temp;
    lily_var *var, *var_temp;

    lit = symtab->lit_start;
    var = symtab->var_start;

    while (lit != NULL) {
        lit_temp = lit->next;

        if (lit->sig->cls->id == SYM_CLASS_STR) {
            lily_strval *sv = (lily_strval *)lit->value.ptr;
            lily_free(sv->str);
            lily_free(sv);
        }

        lily_free(lit);

        lit = lit_temp;
    }

    while (var != NULL) {
        var_temp = var->next;
        cls_id = var->sig->cls->id;

        if (cls_id == SYM_CLASS_METHOD) {
            if (var->sig->node.func == NULL)
                /* It's important to know that @main is a function, but it can't
                   be called so it lacks a func_sig. */
                lily_free(var->sig);
            else
                free_var_func_sig(var->sig);

            lily_method_val *method = (lily_method_val *)var->value.ptr;
            lily_free(method->code);
            lily_free(method);
        }
        else if (cls_id == SYM_CLASS_FUNCTION)
            free_var_func_sig(var->sig);
        else if (cls_id == SYM_CLASS_OBJECT)
            lily_free(var->sig);

        if (var->line_num != 0)
            lily_free(var->name);

        lily_free(var);

        var = var_temp;
    }

    if (symtab->classes != NULL) {
        int i;
        for (i = 0;i <= SYM_LAST_CLASS;i++) {
            lily_class *cls = symtab->classes[i];
            if (cls != NULL) {
                if (cls->storage != NULL) {
                    lily_storage *store_curr = cls->storage;
                    lily_storage *store_start = store_curr;
                    lily_storage *store_next;
                    do {
                        store_next = store_curr->next;
                        if (store_curr->sig->cls->id == SYM_CLASS_OBJECT)
                            lily_free(store_curr->sig);

                        lily_free(store_curr);
                        store_curr = store_next;
                    } while (store_curr != store_start);
                }
                lily_free(cls->sig);
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
            lily_sig *sig;

            sig = lily_malloc(sizeof(lily_sig));
            if (sig != NULL)
                sig->cls = new_class;
            else
                ret = 0;

            new_class->sig = sig;
            new_class->id = i;
            /* try_add_storage checks for the storage being there, since the
               list is circular. */
            new_class->storage = NULL;

            if (ret && !lily_try_add_storage(symtab, new_class))
                ret = 0;

            new_class->name = class_seeds[i];
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
        lily_sig *sig = lily_malloc(sizeof(lily_sig));

        if (new_var == NULL || sig == NULL) {
            lily_free(sig);
            lily_free(new_var);
            ret = 0;
            break;
        }

        if (i == func_count - 1) {
            /* @main is always last, and is the only method. */
            sig->cls = lily_class_by_id(symtab, SYM_CLASS_METHOD);
            sig->node.func = NULL;

            lily_method_val *m = lily_malloc(sizeof(lily_method_val));
            int *code = lily_malloc(4 * sizeof(int));
            if (m == NULL || code == NULL) {
                lily_free(m);
                lily_free(code);
                lily_free(sig);
                lily_free(new_var);
                ret = 0;
                break;
            }
            m->code = code;
            m->pos = 0;
            m->len = 4;
            new_var->value.ptr = m;
        }
        else {
            lily_func_sig *func_sig = lily_malloc(sizeof(lily_func_sig));
            if (func_sig == NULL) {
                lily_free(func_sig);
                lily_free(sig);
                lily_free(new_var);
                ret = 0;
                break;
            }
            sig->node.func = func_sig;
            sig->cls = func_class;
            new_var->value.ptr = seed->func;
            init_func_sig_args(symtab, func_sig, seed);
            if (func_sig->args == NULL) {
                lily_free(func_sig);
                lily_free(sig);
                lily_free(new_var);
                ret = 0;
                break;
            }
        }

        new_var->name = seed->name;
        new_var->sig = sig;
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

    /* Literals are either str, integer, or number, so this is safe. */
    lit->sig = cls->sig;
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

    /* todo: Functions will need this too when they are declarable. */
    lily_sig *sig = try_sig_for_class(cls);
    if (sig == NULL) {
        lily_free(var);
        lily_free(var->name);
        lily_raise_nomem(symtab->error);
    }

    strcpy(var->name, name);

    var->flags = VAR_SYM | S_IS_NIL;
    var->sig = sig;
    var->line_num = *symtab->lex_linenum;

    add_var(symtab, var);
    return var;
}

/* Prepare @lily_main to receive new instructions after a parse step. Debug and
   the vm stay within 'pos', so no need to actually clear the code. */
void lily_reset_main(lily_symtab *symtab)
{
    ((lily_method_val *)symtab->main->value.ptr)->pos = 0;
}
