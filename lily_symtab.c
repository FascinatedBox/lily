#include <string.h>

#include "lily_impl.h"
#include "lily_symtab.h"

/* This creates the *_seed values. */
#include "lily_seed_symtab.h"
#include "lily_pkg_str.h"
/** Symtab is responsible for:
    * Holding all classes, literals, vars, you name it.
    * Using the 'seeds' provided by lily_seed_symtab to initialize the starting
      symbols (@main, literals 0 and 1, etc.).
    * On destruction, destroying all symbols.
    * Symtab currently handles all signature and value derefs. However, this
      may change in the future.
    * Hiding variables when they go out of scope (see lily_drop_block_vars)

    Notes:
    * Functions with 'try' in their name will return NULL or 0 on failure. They
      will never call lily_raise.
    * During symtab initialization, lily_raise cannot be called because the
      parser is not completely allocated and set.
**/

/** Signature and value deref-ing. **/
void lily_deref_sig(lily_sig *sig)
{
    sig->refcount--;
    if (sig->refcount == 0) {
        int cls_id = sig->cls->id;
        if (cls_id == SYM_CLASS_METHOD ||
            cls_id == SYM_CLASS_FUNCTION) {
            lily_call_sig *csig = sig->node.call;
            int i;
            for (i = 0;i < csig->num_args;i++)
                lily_deref_sig(csig->args[i]);

            if (csig->ret != NULL)
                lily_deref_sig(csig->ret);
            lily_free(csig->args);
            lily_free(csig);
        }
        else if (cls_id == SYM_CLASS_LIST) {
            lily_sig *inner_sig = sig->node.value_sig;
            if (inner_sig != NULL)
                lily_deref_sig(inner_sig);
        }
        lily_free(sig);
    }
}

void lily_deref_list_val(lily_sig *sig, lily_list_val *lv)
{
    lv->refcount--;
    if (lv->refcount == 0) {
        int cls_id = sig->node.value_sig->cls->id;
        int i;
        if (cls_id == SYM_CLASS_LIST) {
            for (i = 0;i < lv->num_values;i++)
                lily_deref_list_val(sig->node.value_sig, lv->values[i].list);
        }
        else if (cls_id == SYM_CLASS_STR) {
            for (i = 0;i < lv->num_values;i++)
                lily_deref_str_val(lv->values[i].str);
        }
        else if (cls_id == SYM_CLASS_METHOD) {
            for (i = 0;i < lv->num_values;i++)
                lily_deref_method_val(lv->values[i].method);
        }
        else if (cls_id == SYM_CLASS_OBJECT) {
            for (i = 0;i < lv->num_values;i++)
                lily_deref_object_val(lv->values[i].object);
        }

        lily_free(lv->values);
        lily_free(lv);
    }
}

void lily_deref_method_val(lily_method_val *mv)
{
    mv->refcount--;
    if (mv->refcount == 0) {
        lily_free(mv->code);
        lily_free(mv);
    }
}

void lily_deref_str_val(lily_str_val *sv)
{
    sv->refcount--;
    if (sv->refcount == 0) {
        if (sv->str)
            lily_free(sv->str);
        lily_free(sv);
    }
}

void lily_deref_object_val(lily_object_val *ov)
{
    ov->refcount--;
    if (ov->refcount == 0) {
        if (ov->sig != NULL) {
            lily_deref_unknown_val(ov->sig, ov->value);
            lily_deref_sig(ov->sig);
        }
        lily_free(ov);
    }
}

/* lily_deref_unknown_val
   This is a handy function for doing a deref but not knowing what class the
   sig contained. This should be used to keep redundant checking code. In some
   cases, such as list derefs, hoisting the loops is a better idea for speed. */
void lily_deref_unknown_val(lily_sig *sig, lily_value v)
{
    int cls_id = sig->cls->id;
    if (cls_id == SYM_CLASS_LIST)
        lily_deref_list_val(sig, v.list);
    else if (cls_id == SYM_CLASS_STR)
        lily_deref_str_val(v.str);
    else if (cls_id == SYM_CLASS_METHOD)
        lily_deref_method_val(v.method);
}

/** Symtab init helpers, and shared code **/
/* add_var
   This is a helper function to add a var to the symtab. */
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

/* init_sym_common
   This initializes a sym's value. */
static int init_sym_common(lily_sym *sym, lily_class *cls)
{
    int ret = 1;
    if (cls->id == SYM_CLASS_LIST)
        sym->value.ptr = NULL;
    else if (cls->id == SYM_CLASS_OBJECT) {
        lily_object_val *o = lily_try_new_object_val();
        if (o == NULL)
            ret = 0;

        sym->value.object = o;
    }
    else if (cls->id == SYM_CLASS_METHOD) {
        lily_method_val *m = lily_try_new_method_val();
        if (m == NULL)
            ret = 0;

        sym->value.method = m;
    }
    else if (cls->id == SYM_CLASS_STR)
        sym->value.str = NULL;

    return ret;
}

/* lily_try_add_storage
   This adds a new storage to a given class. The symtab is used to give the
   storage a new id. */
int lily_try_add_storage(lily_symtab *symtab, lily_class *cls)
{
    lily_storage *storage = lily_malloc(sizeof(lily_storage));
    if (storage == NULL)
        return 0;

    lily_sig *sig = lily_try_sig_for_class(cls);
    if (sig == NULL) {
        lily_free(storage);
        return 0;
    }

    if (init_sym_common((lily_sym *)storage, cls) == 0) {
        lily_deref_sig(sig);
        lily_free(storage);
        return 0;
    }

    storage->sig = sig;
    storage->id = symtab->next_storage_id;
    symtab->next_storage_id++;
    storage->flags = STORAGE_SYM | S_IS_NIL;
    storage->expr_num = 0;

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

/* lily_try_new_var
   This creates a new var using the signature given, and copying the name.
   It is okay to pass a sig without list element/call info, since
   lily_try_sig_for_class ensures that important parts are set to NULL.
   Caveats:
   * If this function succeeds, the signature is considered owned by the var
     and will be collected by the symtab later on. However, it does not
     increase the refcount.
   * If this function succeeds, it will also add the var to the symtab to be
     collected later.
   * If it fails, then lily_deref_sig may be necessary to ensure that the sig
     is properly deleted.
   * If the same sig is to be used for multiple vars, the sig needs to be ref'd
     by the caller each time.
   Note: 'try' means this call returns NULL on failure. */
lily_var *lily_try_new_var(lily_symtab *symtab, lily_sig *sig, char *name)
{
    lily_var *var = lily_malloc(sizeof(lily_var));

    if (var == NULL)
        return NULL;

    var->name = lily_malloc(strlen(name) + 1);
    if (var->name == NULL) {
        lily_free(var);
        return NULL;
    }

    if (init_sym_common((lily_sym *)var, sig->cls) == 0) {
        lily_free(var->name);
        lily_free(var);
        return NULL;
    }

    strcpy(var->name, name);

    var->sig = sig;
    var->parent = NULL;
    var->flags = VAR_SYM | S_IS_NIL;
    var->line_num = *symtab->lex_linenum;

    add_var(symtab, var);
    return var;
}

/* try_seed_call_sig
   This function takes a seed and attempts to initialize the given call sig
   from the seed. This function will either leave csig completely initialized,
   or with free-able fields set to NULL (to allow for safe deletion). However,
   it is suggested that the caller just lily_free the csig. */
static int try_seed_call_sig(lily_symtab *symtab,
        lily_func_seed *seed, lily_call_sig *csig)
{
    /* The first arg always exists, and is the return type. */
    int ret;

    ret = 1;
    if (seed->arg_ids[0] == -1)
        csig->ret = NULL;
    else {
        lily_class *c = lily_class_by_id(symtab, seed->arg_ids[0]);
        csig->ret = c->sig;
        c->sig->refcount++;
    }

    if (seed->arg_ids[1] == -1) {
        csig->num_args = 0;
        csig->args = NULL;
    }
    else {
        csig->args = lily_malloc(sizeof(lily_sig *) * seed->num_args);
        if (csig->args != NULL) {
            int i;
            for (i = 1;i <= seed->num_args;i++) {
                lily_class *cls = lily_class_by_id(symtab, seed->arg_ids[i]);
                csig->args[i-1] = cls->sig;
                cls->sig->refcount++;
            }
            csig->num_args = seed->num_args;
        }
        else {
            csig->num_args = 0;
            ret = 0;
        }
    }

    /* This will always be 0 if there are no args. It won't matter if there was
       a problem. However, it's simpler to do it here for all cases. */
    csig->is_varargs = seed->is_varargs;
    return ret;
}

/** Symtab initialization **/
/** During symtab initialization, lily_raise cannot be called, because the
    parser is not completely initialized yet. NULL is returned if it cannot be
    avoided. **/

/* read_seeds
   Symtab init, stage 5 (also helps with 6).
   This function takes in static data (seeds) to create the functions in the
   symtab. An example of seeds can be found in lily_seed_symtab.h */
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
        lily_sig *new_sig = lily_try_sig_for_class(func_class);
        if (new_sig != NULL) {
            lily_var *var = lily_try_new_var(symtab, new_sig, seed->name);

            if (var != NULL) {
                int seed_ret;

                var->flags &= ~(S_IS_NIL);
                var->value.ptr = seed->func;

                seed_ret = try_seed_call_sig(symtab, seed, var->sig->node.call);
                if (seed_ret == 0)
                    ret = 0;
            }
            else {
                lily_deref_sig(new_sig);
                ret = 0;
            }
        }
        else
            ret = 0;
    }

    return ret;
}

/* init_package
   Symtab init, stage 6
   This is called to initialize the str package, but could be used to initialize
   other packages in the future. */
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

/* init_at_main
   Symtab init, stage 4
   This creates @main, which is a hidden method that holds all code that is not
   put inside of a lily method. This is outside of read_seeds since it's the
   only builtin method. It also takes no args.
   @main is always the first var, and thus can always be found at the symtab's
   var_start. */
static int init_at_main(lily_symtab *symtab)
{
    lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_METHOD);
    lily_sig *new_sig = lily_try_sig_for_class(cls);
    if (new_sig == NULL)
        return 0;

    lily_var *var = lily_try_new_var(symtab, new_sig, "@main");

    if (var == NULL) {
        lily_deref_sig(new_sig);
        return 0;
    }

    var->flags &= ~(S_IS_NIL);

    return 1;
}

/* init_literals
   Symtab init, stage 3
   This function creates literals 0 and 1, and always in that order. These are
   used so that and/or ops can be a combo of 'jump_if_true' and 'assign',
   instead of creating a special op for them. */
static int init_literals(lily_symtab *symtab)
{
    int i, ret;
    lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_INTEGER);
    lily_literal *lit;
    ret = 1;

    for (i = 0;i < 2;i++) {
        lit = lily_malloc(sizeof(lily_literal));
        if (lit != NULL) {
            lit->flags = LITERAL_SYM;
            lit->sig = cls->sig;
            lit->sig->refcount++;
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

/* init_classes
   Symtab init, stage 2
   This function initializes the classes of a symtab, as well as their
   signatures. All classes are given a signature so that signatures which don't
   require extra call/internal element info (integer and number, for example),
   can be shared. All a symbol needs to do is sym->sig to get the common
   signature. */
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

            /* Do not use try_sig_for_class, since that assumes that classes
               have sigs. */
            sig = lily_malloc(sizeof(lily_sig));
            if (sig != NULL) {
                sig->cls = new_class;
                sig->refcount = 1;
            }
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

            new_class->name = class_seeds[i].name;
            new_class->is_refcounted = class_seeds[i].is_refcounted;
        }
        else
            ret = 0;

        classes[i] = new_class;
    }

    return ret;
}

/* lily_new_symtab:
   Symtab init, stage 1
   This function is responsible for creating a symtab struct for the parser.
   Returns a valid symtab object, or NULL on failure. */
lily_symtab *lily_new_symtab(lily_raiser *raiser)
{
    lily_symtab *s = lily_malloc(sizeof(lily_symtab));
    int v = 0;

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
    /* lily_try_new_var expects lex_linenum to be the lexer's line number.
       0 is used, because these are all builtins, and the lexer may have failed
       to initialize anyway. */
    s->lex_linenum = &v;

    if (!init_classes(s) || !init_literals(s) ||
        !init_at_main(s) || !read_seeds(s, builtin_seeds, NUM_BUILTIN_SEEDS) ||
        !init_package(s, SYM_CLASS_STR, str_seeds, NUM_STR_SEEDS)) {
        /* This will free any symbols added, and the symtab object. */
        lily_free_symtab(s);
        return NULL;
    }

    s->raiser = raiser;

    return s;
}

/** Symtab free-ing **/
/* free_sym_common
   This handles the common value deref-ing needed by lily_free_symtab. */
static void free_sym_common(lily_sym *sym)
{
    int cls_id = sym->sig->cls->id;

    if (cls_id == SYM_CLASS_METHOD) {
        lily_method_val *mv = sym->value.method;
        /* This is currently safe, because method vars aren't created if a
           method val can't be created for them. */
        lily_deref_method_val(mv);
    }
    else if (cls_id == SYM_CLASS_LIST) {
        lily_list_val *lv = sym->value.list;
        if (lv != NULL)
            lily_deref_list_val(sym->sig, lv);
    }
    else if (cls_id == SYM_CLASS_STR) {
        lily_str_val *sv = sym->value.str;
        if (sv != NULL)
            lily_deref_str_val(sv);
    }
    else if (cls_id == SYM_CLASS_OBJECT) {
        lily_object_val *ov = sym->value.object;
        if (ov != NULL)
            lily_deref_object_val(ov);
    }

    lily_deref_sig(sym->sig);
}

/* free_vars
   This holds common code to free a linked list of vars. When the symtab is
   being free'd, this is called on the table of old vars and the active vars. */
void free_vars(lily_var *var)
{
    lily_var *var_temp;

    while (var != NULL) {
        var_temp = var->next;
        free_sym_common((lily_sym *)var);

        lily_free(var->name);
        lily_free(var);

        var = var_temp;
    }
}

/* lily_free_symtab
   As the name suggests, this destroys the symtab. Some init stuff may have
   failed, so the NULL checking is important. */
void lily_free_symtab(lily_symtab *symtab)
{
    lily_literal *lit, *lit_temp;

    lit = symtab->lit_start;

    while (lit != NULL) {
        lit_temp = lit->next;

        free_sym_common((lily_sym *)lit);
        lily_free(lit);

        lit = lit_temp;
    }

    if (symtab->classes != NULL) {
        int i;
        for (i = 0;i <= SYM_LAST_CLASS;i++) {
            lily_class *cls = symtab->classes[i];
            if (cls != NULL) {
                if (cls->call_start != NULL)
                    free_vars(cls->call_start);
                /* Remember that storages are circularly linked. So free them
                   until we run in the first storage twice. */
                if (cls->storage != NULL) {
                    lily_storage *store_curr = cls->storage;
                    lily_storage *store_start = store_curr;
                    lily_storage *store_next;
                    do {
                        store_next = store_curr->next;
                        free_sym_common((lily_sym *)store_curr);
                        lily_free(store_curr);
                        store_curr = store_next;
                    } while (store_curr != store_start);
                }
            }
        }
    }

    if (symtab->var_start != NULL)
        free_vars(symtab->var_start);
    if (symtab->old_var_start != NULL)
        free_vars(symtab->old_var_start);

    /* At this point, there's nothing left to use the signatures of the
       classes. These signatures can't be deref'd though, because lily_deref_sig
       was causing errors in valgrind. Plus, this also ensures that everything
       is free'd even if there are extra refs. */
    if (symtab->classes != NULL) {
        int i;
        for (i = 0;i <= SYM_LAST_CLASS;i++) {
            lily_class *cls = symtab->classes[i];
            if (cls != NULL) {
                lily_free(cls->sig);
                lily_free(cls);
            }
        }
        lily_free(symtab->classes);
    }

    lily_free(symtab);
}

/** Functions provided by symtab for other modules. **/

/* try_sig_for_class
   If the given class does not require extra data (like how lists need an inner
   element, calls need args, etc), then this will return the shared signature
   of a class and bump the refcount.
   If the signature will require complex data, an attempt is made at allocating
   a new signature with 1 ref.
   Note: 'try' means this call returns NULL on failure. */
lily_sig *lily_try_sig_for_class(lily_class *cls)
{
    lily_sig *sig;

    if (cls->id == SYM_CLASS_OBJECT || cls->id == SYM_CLASS_LIST) {
        sig = lily_malloc(sizeof(lily_sig));
        if (sig != NULL) {
            sig->cls = cls;
            sig->node.value_sig = NULL;
            sig->refcount = 1;
        }
    }
    else if (cls->id == SYM_CLASS_FUNCTION ||
             cls->id == SYM_CLASS_METHOD) {
        sig = lily_malloc(sizeof(lily_sig));
        if (sig != NULL) {
            lily_call_sig *csig;
            sig->refcount = 1;
            csig = lily_malloc(sizeof(lily_call_sig));
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
    else {
        sig = cls->sig;
        sig->refcount++;
    }

    return sig;
}

/* lily_try_new_method_val
   This will attempt to create a new method value (for storing code and the
   position of it).
   Note: 'try' means this call returns NULL on failure. */
lily_method_val *lily_try_new_method_val()
{
    lily_method_val *m = lily_malloc(sizeof(lily_method_val));
    uintptr_t *code = lily_malloc(8 * sizeof(uintptr_t));

    if (m == NULL || code == NULL) {
        lily_free(m);
        lily_free(code);
        return NULL;
    }

    m->refcount = 1;
    m->code = code;
    m->pos = 0;
    m->len = 8;
    return m;
}

/* lily_try_new_object_val
   This tries to create a new object value.
   Note: 'try' means this call returns NULL on failure. */
lily_object_val *lily_try_new_object_val()
{
    lily_object_val *o = lily_malloc(sizeof(lily_object_val));

    if (o == NULL)
        return NULL;

    o->refcount = 1;
    o->sig = NULL;
    o->value.integer = 0;

    return o;
}

/* lily_class_by_id
   This function will return a class for a particular class id. This function
   will need to be updated when users can create their own classes. */
lily_class *lily_class_by_id(lily_symtab *symtab, int class_id)
{
    return symtab->classes[class_id];
}

/* lily_class_by_name
   This function returns a class for a given name, or NULL. */
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

/* lily_find_class_callable
   This function will see if a given clas has a function with the given name.
   NULL is returned on failure. */
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

/* lily_keyword_by_name
   Attempt to lookup a keyword based on a name. Keywords are in a static list,
   (via lily_seed_symtab.h), so this doesn't require a symtab. */
int lily_keyword_by_name(char *name)
{
    int i;
    for (i = 0;i <= KEY_LAST_ID;i++) {
        if (strcmp(keywords[i], name) == 0)
            return i;
    }

    return -1;
}

/* lily_var_by_name
   Search the symtab for a var with a name of 'name'. This will return the var
   or NULL. */
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

/* lily_new_literal
   This adds a new literal to the given symtab. The literal will be of the class
   'cls', and be given the value 'value'. The symbol created does not have
   S_IS_NIL set, because the literal is assumed to never be nil.
   This function currently handles only integer, number, and str values.
   Warning: This function calls lily_raise_nomem instead of returning NULL. */
lily_literal *lily_new_literal(lily_symtab *symtab, lily_class *cls,
        lily_value value)
{
    lily_literal *lit = lily_malloc(sizeof(lily_literal));
    if (lit == NULL) {
        /* Make sure any str sent will be properly free'd. */
        if (cls->id == SYM_CLASS_STR) {
            lily_str_val *sv = value.str;
            lily_free(sv->str);
            lily_free(sv);
        }
        lily_raise_nomem(symtab->raiser);
    }
    /* Literals are either str, integer, or number, so this is safe. */
    lit->sig = cls->sig;
    /* ...as long as the refcount is bumped. */
    cls->sig->refcount++;

    lit->flags = LITERAL_SYM;
    lit->next = NULL;

    if (symtab->lit_top == NULL)
        symtab->lit_start = lit;
    else
        symtab->lit_top->next = lit;

    symtab->lit_top = lit;

    symtab->lit_top = lit;
    lit->id = symtab->next_lit_id;
    symtab->next_lit_id++;

    return lit;
}

/* lily_drop_block_vars
   This function will take the vars from 'start' forward, and put them in the
   symtab's list of old vars. This is called by emitter when a if/elif/else/etc.
   block closes so that the symtab won't find them anymore. */
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
