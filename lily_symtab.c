#include <string.h>

#include "lily_impl.h"
#include "lily_symtab.h"

/* This creates the *_seed values. */
#include "lily_seed_symtab.h"
#include "lily_pkg_str.h"

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

static lily_call_sig *try_seed_call_sig(lily_symtab *symtab,
        lily_func_seed *seed)
{
    /* The first arg always exists, and is the return type. */
    int i;

    lily_call_sig *csig = lily_malloc(sizeof(lily_call_sig));
    if (csig == NULL)
        return NULL;

    if (seed->arg_ids[0] == -1)
        csig->ret = NULL;
    else {
        lily_class *c = lily_class_by_id(symtab, seed->arg_ids[0]);
        csig->ret = c->sig;
    }

    if (seed->arg_ids[1] == -1) {
        csig->num_args = 0;
        csig->args = NULL;
    }
    else {
        csig->args = lily_malloc(sizeof(lily_sig *) * seed->num_args);
        if (csig->args == NULL) {
            lily_free(csig);
            return NULL;
        }

        for (i = 1;i <= seed->num_args;i++) {
            lily_class *cls = lily_class_by_id(symtab, seed->arg_ids[i]);
            csig->args[i-1] = cls->sig;
        }
        csig->num_args = seed->num_args;
    }
    csig->is_varargs = seed->is_varargs;
    return csig;
}

/* All other signatures that vars use are copies of one held by a class. Those
   will be free'd with the class. */
static void free_var_func_sig(lily_sig *sig)
{
    lily_call_sig *csig = sig->node.call;
    lily_free(csig->args);
    lily_free(csig);
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
    else if (cls->id == SYM_CLASS_METHOD) {
        sig = lily_malloc(sizeof(lily_sig));
        if (sig != NULL) {
            lily_call_sig *csig = lily_malloc(sizeof(lily_call_sig));
            if (csig != NULL) {
                csig->ret = NULL;
                csig->args = NULL;
                csig->num_args = 0;
                csig->is_varargs = 0;
                sig->cls = cls;
                sig->node.call = csig;
            }
            else {
                lily_free(sig);
                sig = NULL;
            }
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

    /* str's concat function will check to see if there is a strval to save the
       new string to. For now, set this to null so there's no invalid read when
       concat does that. */
    if (cls->id == SYM_CLASS_STR)
        storage->value.ptr = NULL;

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

lily_method_val *lily_try_new_method_val(lily_symtab *symtab)
{
    lily_method_val *m = lily_malloc(sizeof(lily_method_val));
    uintptr_t *code = lily_malloc(8 * sizeof(uintptr_t));

    if (m == NULL || code == NULL) {
        lily_free(m);
        lily_free(code);
        return NULL;
    }

    m->code = code;
    m->pos = 0;
    m->len = 8;
    return m;
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

void free_vars(lily_var *var)
{
    int cls_id;
    lily_var *var_temp;

    while (var != NULL) {
        var_temp = var->next;
        cls_id = var->sig->cls->id;

        if (cls_id == SYM_CLASS_METHOD) {
            free_var_func_sig(var->sig);
            lily_method_val *method = (lily_method_val *)var->value.ptr;
            /* lily_new_var doesn't create new methods without a method value,
               so this is safe. */
            lily_free(method->code);
            lily_free(method);
        }
        else if (cls_id == SYM_CLASS_FUNCTION)
            free_var_func_sig(var->sig);
        else if (cls_id == SYM_CLASS_OBJECT)
            lily_free(var->sig);
        else if (cls_id == SYM_CLASS_STR) {
            lily_strval *sv = (lily_strval *)var->value.ptr;
            if (sv != NULL)
                lily_deref_strval(sv);
        }

        if (var->line_num != 0)
            lily_free(var->name);

        lily_free(var);

        var = var_temp;
    }
}

void lily_free_symtab(lily_symtab *symtab)
{
    lily_literal *lit, *lit_temp;

    lit = symtab->lit_start;

    while (lit != NULL) {
        lit_temp = lit->next;

        if (lit->sig->cls->id == SYM_CLASS_STR)
            lily_deref_strval((lily_strval *)lit->value.ptr);

        lily_free(lit);

        lit = lit_temp;
    }

    if (symtab->var_start != NULL)
        free_vars(symtab->var_start);
    if (symtab->old_var_start != NULL)
        free_vars(symtab->old_var_start);

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
                        else if (store_curr->sig->cls->id == SYM_CLASS_METHOD)
                            free_var_func_sig(store_curr->sig);
                        else if (store_curr->sig->cls->id == SYM_CLASS_STR) {
                            lily_strval *sv = store_curr->value.ptr;
                            if (sv != NULL)
                                lily_deref_strval(sv);
                        }
                        lily_free(store_curr);
                        store_curr = store_next;
                    } while (store_curr != store_start);
                }
                if (cls->call_start != NULL)
                    free_vars(cls->call_start);

                lily_free(cls->sig);
                lily_free(cls);
            }
        }
        lily_free(symtab->classes);
    }

    lily_free(symtab);
}

int lily_keyword_by_name(char *name)
{
    int i;
    for (i = 0;i <= KEY_LAST_ID;i++) {
        if (strcmp(keywords[i], name) == 0)
            return i;
    }

    return -1;
}

static int init_classes(lily_symtab *symtab)
{
    int i, class_count, ret;
    lily_class **classes;

    classes = lily_malloc(sizeof(lily_class *) * (SYM_LAST_CLASS+1));
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

            new_class->call_start = NULL;
            new_class->call_top = NULL;
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

static int init_literals(lily_symtab *symtab)
{
    int i, ret;
    lily_literal *lit;
    ret = 1;

    for(i = 0; i < 2;i++) {
        lit = lily_malloc(sizeof(lily_literal));
        if (lit != NULL) {
            lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_INTEGER);
            lit->flags = LITERAL_SYM;
            lit->sig = cls->sig;
            lit->value.integer = i;
            lit->next = NULL;

            if (symtab->lit_start == NULL)
                symtab->lit_start = lit;
            else
                symtab->lit_top->next = lit;

            symtab->lit_top = lit;
            lit->id = symtab->next_lit_id;
            symtab->next_lit_id++;
        }
        else
            ret = 0;

    }

    return ret;
}

static int read_seeds(lily_symtab *symtab, lily_func_seed **seeds,
        int seed_count)
{
    /* Turn the keywords into symbols. */
    int i, ret;
    lily_class *func_class;

    func_class = lily_class_by_id(symtab, SYM_CLASS_FUNCTION);
    ret = 1;

    for (i = 0;i < seed_count;i++) {
        lily_func_seed *seed = seeds[i];
        lily_var *new_var = lily_malloc(sizeof(lily_var));
        lily_sig *sig = lily_malloc(sizeof(lily_sig));
        /* This function returns NULL if it can't allocate args, so don't check
           for NULL args here. */
        lily_call_sig *csig = try_seed_call_sig(symtab, seed);

        if (new_var == NULL || sig == NULL || csig == NULL) {
            lily_free(sig);
            lily_free(new_var);
            if (csig != NULL) {
                lily_free(csig);
                lily_free(csig->args);
            }
            ret = 0;
            break;
        }

        if (seed->func != NULL) {
            sig->cls = func_class;
            new_var->value.ptr = seed->func;
        }
        else {
            /* This is @main, which is a method. Since it's a method, it
               doesn't have ->func set to anything. */
            lily_method_val *m = lily_try_new_method_val(symtab);
            if (m == NULL) {
                lily_free(csig);
                lily_free(sig);
                lily_free(new_var);
                ret = 0;
                break;
            }
            m->first_arg = NULL;
            m->last_arg = NULL;
            new_var->value.ptr = m;
            sig->cls = lily_class_by_id(symtab, SYM_CLASS_METHOD);
        }

        sig->node.call = csig;
        new_var->name = seed->name;
        new_var->sig = sig;
        new_var->line_num = 0;
        new_var->flags = VAR_SYM;
        add_var(symtab, new_var);
    }

    return ret;
}

int init_package(lily_symtab *symtab, int cls_id, lily_func_seed **seeds,
        int num_seeds)
{
    lily_var *save_top = symtab->var_top;
    lily_class *cls = lily_class_by_id(symtab, cls_id);
    int ret = read_seeds(symtab, seeds, num_seeds);

    if (ret) {
        /* The functions were created as regular global vars. Make them all
           class-local. */
        lily_var *v;
        cls->call_start = save_top->next;
        cls->call_top = symtab->var_top;
        symtab->var_top = save_top;
        save_top->next = NULL;
        for (v = cls->call_start;v != NULL;v = v->next)
            v->parent = cls;
    }

    return ret;
}

lily_symtab *lily_new_symtab(lily_raiser *raiser)
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
    s->old_var_start = NULL;
    s->old_var_top = NULL;

    if (!init_classes(s) || !init_literals(s) ||
        !read_seeds(s, builtin_seeds, NUM_BUILTIN_SEEDS) ||
        !init_package(s, SYM_CLASS_STR, str_seeds, NUM_STR_SEEDS)) {
        /* This will free any symbols added, and the symtab object. */
        lily_free_symtab(s);
        return NULL;
    }

    s->raiser = raiser;

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

lily_literal *lily_new_literal(lily_symtab *symtab, lily_class *cls, lily_value value)
{
    lily_literal *lit = lily_malloc(sizeof(lily_literal));
    if (lit == NULL) {
        if (cls->id == SYM_CLASS_STR) {
            lily_strval *sv = (lily_strval *)value.ptr;
            lily_free(sv->str);
            lily_free(sv);
        }
        lily_raise_nomem(symtab->raiser);
    }
    /* Literals are either str, integer, or number, so this is safe. */
    lit->sig = cls->sig;
    lit->flags = LITERAL_SYM;
    lit->next = NULL;

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
        lily_raise_nomem(symtab->raiser);

    var->name = lily_malloc(strlen(name) + 1);
    if (var->name == NULL) {
        lily_free(var);
        lily_raise_nomem(symtab->raiser);
    }

    lily_sig *sig = try_sig_for_class(cls);
    if (sig == NULL) {
        lily_free(var->name);
        lily_free(var);
        lily_raise_nomem(symtab->raiser);
    }

    if (cls->id == SYM_CLASS_STR)
        var->value.ptr = NULL;
    else if (cls->id == SYM_CLASS_METHOD) {
        lily_method_val *m = lily_try_new_method_val(symtab);
        if (m == NULL) {
            free_var_func_sig(sig);
            lily_free(var->name);
            lily_free(var);
            lily_raise_nomem(symtab->raiser);
        }
        var->value.ptr = m;
    }

    strcpy(var->name, name);

    var->parent = NULL;
    var->flags = VAR_SYM | S_IS_NIL;
    var->sig = sig;
    var->line_num = *symtab->lex_linenum;

    add_var(symtab, var);
    return var;
}

int lily_drop_block_vars(lily_symtab *symtab, lily_var *start)
{
    if (symtab->old_var_start == NULL)
        /* This becomes the list of vars. */
        symtab->old_var_start = start->next;
    else
        /* Since there's a list of old vars, add the first to the end. */
        symtab->old_var_top->next = start->next;

    int ret = symtab->var_top->id - start->id;
    /* Put this at the end again, so new vars aren't lost. */
    symtab->old_var_top = symtab->var_top;
    symtab->var_top = start;
    /* Detach old and new vars. */
    start->next = NULL;
    return ret;
}

void lily_deref_strval(lily_strval *sv)
{
    sv->refcount--;
    if (sv->refcount == 0) {
        if (sv->str)
            lily_free(sv->str);
        lily_free(sv);
    }
}

lily_var *lily_find_class_callable(lily_class *cls, char *name)
{
    lily_var *iter = cls->call_start;
    if (iter != NULL) {
        for (;iter != NULL;iter = iter->next) {
            if (strcmp(iter->name, name) == 0)
                break;
        }
    }

    return iter;
}
