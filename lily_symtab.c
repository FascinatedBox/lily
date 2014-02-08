#include <string.h>

#include "lily_impl.h"
#include "lily_symtab.h"

/* This creates the *_seed values. */
#include "lily_seed_symtab.h"
#include "lily_pkg_str.h"
#include "lily_vm.h"

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

/** Value deref-ing. **/
void lily_deref_list_val(lily_sig *sig, lily_list_val *lv)
{
    lv->refcount--;
    if (lv->refcount == 0) {
        int cls_id = sig->node.value_sig->cls->id;
        int i;
        if (cls_id == SYM_CLASS_LIST) {
            for (i = 0;i < lv->num_values;i++) {
                if (lv->flags[i] == 0)
                    lily_deref_list_val(sig->node.value_sig,
                            lv->values[i].list);
            }
        }
        else if (cls_id == SYM_CLASS_STR) {
            for (i = 0;i < lv->num_values;i++)
                if (lv->flags[i] == 0)
                    lily_deref_str_val(lv->values[i].str);
        }
        else if (cls_id == SYM_CLASS_METHOD) {
            for (i = 0;i < lv->num_values;i++)
                if (lv->flags[i] == 0)
                    lily_deref_method_val(lv->values[i].method);
        }
        else if (cls_id == SYM_CLASS_OBJECT) {
            for (i = 0;i < lv->num_values;i++) {
                if (lv->flags[i] == 0)
                    lily_deref_object_val(lv->values[i].object);
                else if (lv->flags[i] & SYM_IS_CIRCULAR) {
                    /* Objects are containers that are not shared across lists.
                       The circularity applies to the value held in the object,
                       not the object itself. */
                    lily_object_val *ov = lv->values[i].object;
                    lily_free(ov);
                }
            }
        }

        lily_free(lv->flags);
        lily_free(lv->values);
        lily_free(lv);
    }
}

/* lily_deref_list_val_by
   This is called to drop a list value by a given number of references. vm
   currently uses this for fixing the refcount of a list after checking for
   circular refs. */
void lily_deref_list_val_by(lily_sig *sig, lily_list_val *lv, int drop)
{
    if (drop == lv->refcount) {
        lv->refcount = 1;
        lily_deref_list_val(sig, lv);
    }
    else
        lv->refcount -= drop;
}

void lily_deref_method_val(lily_method_val *mv)
{
    mv->refcount--;
    if (mv->refcount == 0) {
        if (mv->reg_info != NULL) {
            int i;
            for (i = 0;i < mv->reg_count;i++)
                lily_free(mv->reg_info[i].name);
        }

        lily_free(mv->reg_info);
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
        if (ov->sig != NULL)
            lily_deref_unknown_val(ov->sig, ov->value);

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
    else if (cls_id == SYM_CLASS_OBJECT)
        lily_deref_object_val(v.object);
}

/** Symtab init helpers, and shared code **/
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

    var->flags = symtab->scope | SYM_IS_NIL | SYM_TYPE_VAR;
    strcpy(var->name, name);
    var->line_num = *symtab->lex_linenum;

    var->method_depth = symtab->method_depth;
    var->sig = sig;
    var->next = NULL;
    var->reg_spot = symtab->next_register_spot;
    symtab->next_register_spot++;

    if (symtab->var_start == NULL)
        symtab->var_start = var;
    else
        symtab->var_top->next = var;

    symtab->var_top = var;

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
        lily_sig *new_sig = lily_try_sig_for_class(symtab, func_class);
        if (new_sig != NULL) {
            lily_var *var = lily_try_new_var(symtab, new_sig, seed->name);

            if (var != NULL) {
                int seed_ret;

                var->value.function = lily_try_new_function_val(seed->func, 
                        seed->name);

                if (var->value.function != NULL) {
                    var->flags &= ~(SYM_IS_NIL);
                    seed_ret = try_seed_call_sig(symtab, seed,
                            var->sig->node.call);

                    if (seed_ret == 0)
                        ret = 0;
                }
                else
                    ret = 0;
            }
            else
                ret = 0;
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
        cls->call_start = save_top->next;
        cls->call_top = symtab->var_top;
        symtab->var_top = save_top;
        save_top->next = NULL;
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
    lily_sig *new_sig = lily_try_sig_for_class(symtab, cls);
    if (new_sig == NULL)
        return 0;

    lily_var *var = lily_try_new_var(symtab, new_sig, "@main");

    if (var == NULL)
        return 0;

    /* It would be rather silly to load @main into a register since it's not
       callable. This will cause the next var to occupy the register that @main
       would have gotten, so there's no gap. */
    symtab->next_register_spot--;
    /* The emitter will mark @main as non-nil when it's entered. Until then,
       leave it alone because it doesn't have a value. */

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
            lit->flags = SYM_TYPE_LITERAL;
            lit->sig = cls->sig;
            lit->value.integer = i;
            lit->next = NULL;

            if (symtab->lit_start == NULL)
                symtab->lit_start = lit;
            else
                symtab->lit_top->next = lit;

            symtab->lit_top = lit;
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

    classes = lily_malloc(sizeof(lily_class *) * INITIAL_CLASS_SIZE);
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
                /* So that testing this later doesn't potentially cause an
                   invalid read... */
                sig->node.value_sig = NULL;

                /* Link the signatures to the root sig too, so that they can be
                   deleted. */
                sig->next = symtab->root_sig;
                symtab->root_sig = sig;
            }
            else
                ret = 0;

            new_class->call_start = NULL;
            new_class->call_top = NULL;
            new_class->sig = sig;
            new_class->id = i;

            new_class->name = class_seeds[i].name;
            new_class->is_refcounted = class_seeds[i].is_refcounted;
        }
        else
            ret = 0;

        classes[i] = new_class;
    }

    /* This is so symtab cleanup catches all of the builtin classes, regardless
       of what parts were initialized. */
    symtab->class_pos = SYM_LAST_CLASS;
    return ret;
}

/* lily_new_symtab:
   Symtab init, stage 1
   This function is responsible for creating a symtab struct for the parser.
   Returns a valid symtab object, or NULL on failure. */
lily_symtab *lily_new_symtab(lily_raiser *raiser)
{
    lily_symtab *symtab = lily_malloc(sizeof(lily_symtab));
    int v = 0;

    if (symtab == NULL)
        return NULL;

    symtab->next_register_spot = 0;
    symtab->scope = SYM_SCOPE_GLOBAL;
    symtab->class_pos = 0;
    symtab->class_size = INITIAL_CLASS_SIZE;
    symtab->var_start = NULL;
    symtab->var_top = NULL;
    symtab->old_method_chain = NULL;
    symtab->classes = NULL;
    symtab->lit_start = NULL;
    symtab->lit_top = NULL;
    symtab->method_depth = 1;
    /* lily_try_new_var expects lex_linenum to be the lexer's line number.
       0 is used, because these are all builtins, and the lexer may have failed
       to initialize anyway. */
    symtab->lex_linenum = &v;
    symtab->root_sig = NULL;

    if (!init_classes(symtab) || !init_literals(symtab) ||
        !init_at_main(symtab) ||
        !read_seeds(symtab, builtin_seeds, NUM_BUILTIN_SEEDS) ||
        !init_package(symtab, SYM_CLASS_STR, str_seeds, NUM_STR_SEEDS)) {
        /* This will free any symbols added, and the symtab object. */
        lily_free_symtab(symtab);
        return NULL;
    }

    symtab->raiser = raiser;

    return symtab;
}

/** Symtab free-ing **/
/* free_vars
   This holds common code to free a linked list of vars. When the symtab is
   being free'd, this is called on the table of old vars and the active vars. */
void free_vars(lily_var *var)
{
    lily_var *var_temp;

    while (var != NULL) {
        var_temp = var->next;
        if ((var->flags & SYM_IS_NIL) == 0) {
            int cls_id = var->sig->cls->id;
            if (cls_id == SYM_CLASS_METHOD)
                lily_deref_method_val(var->value.method);
            else if (cls_id == SYM_CLASS_FUNCTION)
                lily_free(var->value.function);
        }
        lily_free(var->name);
        lily_free(var);

        var = var_temp;
    }
}

static void free_at_main(lily_method_val *mv)
{
    lily_free(mv->reg_info);
    lily_free(mv->code);
    lily_free(mv);
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

        if (lit->sig->cls->is_refcounted)
            lily_deref_str_val(lit->value.str);

        lily_free(lit);

        lit = lit_temp;
    }

    if (symtab->classes != NULL) {
        int i;
        for (i = 0;i <= symtab->class_pos;i++) {
            lily_class *cls = symtab->classes[i];
            if (cls != NULL && cls->call_start != NULL)
                free_vars(cls->call_start);
        }
    }

    lily_method_val *main_method;

    if (symtab->var_start &&
        ((symtab->var_start->flags & SYM_IS_NIL) == 0))
        main_method = symtab->var_start->value.method;
    else
        main_method = NULL;

    if (symtab->var_start != NULL)
        free_vars(symtab->var_start);
    if (symtab->old_method_chain != NULL)
        free_vars(symtab->old_method_chain);
    if (main_method != NULL)
        free_at_main(main_method);

    lily_sig *sig, *sig_temp;

    /* Destroy the signatures before the classes, since the sigs need to check
       the class id to make sure there isn't a call sig to destroy. */
    sig = symtab->root_sig;
    int j = 0;
    while (sig != NULL) {
        j++;
        sig_temp = sig->next;
        if (sig->cls->id == SYM_CLASS_METHOD ||
            sig->cls->id == SYM_CLASS_FUNCTION) {
            lily_call_sig *csig = sig->node.call;
            if (csig != NULL) {
                lily_free(csig->args);
                lily_free(csig);
            }
        }
        lily_free(sig);
        sig = sig_temp;
    }

    if (symtab->classes != NULL) {
        int i;
        for (i = 0;i <= SYM_LAST_CLASS;i++) {
            lily_class *cls = symtab->classes[i];
            if (cls != NULL)
                lily_free(cls);
        }
        lily_free(symtab->classes);
    }

    lily_free(symtab);
}

/** Functions provided by symtab for other modules. **/
lily_literal *lily_get_line_literal(lily_symtab *symtab)
{
    int line_num = *(symtab->lex_linenum);
    lily_literal *lit, *ret;
    ret = NULL;

    for (lit = symtab->lit_start;lit;lit = lit->next) {
        if (lit->sig->cls == SYM_CLASS_INTEGER &&
            lit->value.integer == line_num) {
            ret = lit;
            break;
        }
    }

    if (ret == NULL) {
        lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_INTEGER);
        /* lily_new_literal is guaranteed to work or raise nomem, so this is
           safe. */
        lily_value v;
        v.integer = line_num;
        ret = lily_new_literal(symtab, cls, v);
        ret->value = v;
    }

    return ret;
}

lily_literal *lily_get_str_literal(lily_symtab *symtab, char *name)
{
    int name_len;
    lily_literal *lit, *ret;
    name_len = strlen(name);
    ret = NULL;

    for (lit = symtab->lit_start;lit;lit = lit->next) {
        if (lit->sig->cls->id == SYM_CLASS_STR &&
            lit->value.str->size == name_len &&
            strcmp(lit->value.str->str, name) == 0) {
            ret = lit;
            break;
        }
    }

    if (ret == NULL) {
        lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_STR);
        /* lily_new_literal is guaranteed to work or raise nomem, so this is
           safe. */
        char *str_buffer = lily_malloc((name_len + 1) * sizeof(char));
        lily_str_val *sv = lily_malloc(sizeof(lily_str_val));
        if (sv == NULL || str_buffer == NULL) {
            lily_free(sv);
            lily_free(str_buffer);
            lily_raise_nomem(symtab->raiser);
        }

        strcpy(str_buffer, name);
        sv->str = str_buffer;
        sv->size = name_len;
        sv->refcount = 1;

        lily_value v;
        v.str = sv;
        ret = lily_new_literal(symtab, cls, v);
    }

    return ret;
}

/* try_sig_for_class
   If the given class does not require extra data (like how lists need an inner
   element, calls need args, etc), then this will return the shared signature
   of a class and bump the refcount.
   If the signature will require complex data, an attempt is made at allocating
   a new signature with 1 ref.
   Note: 'try' means this call returns NULL on failure. */
lily_sig *lily_try_sig_for_class(lily_symtab *symtab, lily_class *cls)
{
    lily_sig *sig;

    if (cls->id == SYM_CLASS_OBJECT || cls->id == SYM_CLASS_LIST) {
        sig = lily_malloc(sizeof(lily_sig));
        if (sig != NULL) {
            sig->cls = cls;
            sig->node.value_sig = NULL;

            sig->next = symtab->root_sig;
            symtab->root_sig = sig;
        }
    }
    else if (cls->id == SYM_CLASS_FUNCTION ||
             cls->id == SYM_CLASS_METHOD) {
        sig = lily_malloc(sizeof(lily_sig));
        if (sig != NULL) {
            lily_call_sig *csig;
            csig = lily_malloc(sizeof(lily_call_sig));
            if (csig != NULL) {
                csig->ret = NULL;
                csig->args = NULL;
                csig->num_args = 0;
                csig->is_varargs = 0;
                sig->cls = cls;
                sig->node.call = csig;

                sig->next = symtab->root_sig;
                symtab->root_sig = sig;
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

/* lily_try_new_function_val
   This will attempt to create a new function value (for storing a function
   pointer and a name for it).
   Note: 'try' means this call returns NULL on failure. */
lily_function_val *lily_try_new_function_val(lily_func func, char *name)
{
    lily_function_val *f = lily_malloc(sizeof(lily_function_val));

    if (f == NULL) {
        lily_free(f);
        return NULL;
    }

    f->refcount = 1;
    f->func = func;
    f->trace_name = name;
    return f;
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

    m->reg_info = NULL;
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
   This function will return a class for a particular class id. This is
   typically used to quickly fetch builtin classes. */
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

    for (i = 0;i <= symtab->class_pos;i++) {
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
        if (var->name != NULL &&
            strcmp(var->name, name) == 0 &&
            ((var->flags & SYM_OUT_OF_SCOPE) == 0))
            return var;
        var = var->next;
    }

    return NULL;
}

/* lily_new_literal
   This adds a new literal to the given symtab. The literal will be of the class
   'cls', and be given the value 'value'. The symbol created does not have
   SYM_IS_NIL set, because the literal is assumed to never be nil.
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

    lit->flags = SYM_TYPE_LITERAL;
    lit->next = NULL;
    lit->value = value;
    /* Literals are never saved to a register. */
    lit->reg_spot = -1;

    if (symtab->lit_top == NULL)
        symtab->lit_start = lit;
    else
        symtab->lit_top->next = lit;

    symtab->lit_top = lit;

    return lit;
}

void lily_hide_block_vars(lily_symtab *symtab, lily_var *start)
{
    start = start->next;

    /* The current method will need the vars later to set the reg_info part of
       it. This is much, much easier if the vars are never moved to another
       table, so mark them out of scope for now. */
    while (start) {
        start->flags |= SYM_OUT_OF_SCOPE;
        start = start->next;
    }
}

/* lily_sigequal
   This function checks to see if two signatures hold the same information. */
int lily_sigequal(lily_sig *lhs, lily_sig *rhs)
{
    int ret;

    if (lhs == rhs)
        ret = 1;
    else {
        if (lhs->cls->id == rhs->cls->id) {
            if (lhs->cls->id == SYM_CLASS_LIST) {
                if (lily_sigequal(lhs->node.value_sig,
                                  rhs->node.value_sig)) {
                    ret = 1;
                }
                else
                    ret = 0;
            }
            else if (lhs->cls->id == SYM_CLASS_METHOD ||
                     lhs->cls->id == SYM_CLASS_FUNCTION) {
                lily_call_sig *lhs_csig = lhs->node.call;
                lily_call_sig *rhs_csig = rhs->node.call;
                int lhs_num_args = lhs_csig->num_args;

                if (lhs_num_args != rhs_csig->num_args ||
                    lhs_csig->is_varargs != rhs_csig->is_varargs)
                    ret = 0;
                else {
                    /* ret being NULL indicates that the method does not have
                       a return value. lily_sigequal doesn't test this, because
                       nobody else sends NULL sigs to test.
                       The NULL checks are necessary because one of the sigs
                       could be NULL (comparing a method that returns a value
                       with one that does not, for example). */
                    if (lhs_csig->ret != rhs_csig->ret &&
                        (lhs_csig->ret == NULL || rhs_csig->ret == NULL ||
                         lily_sigequal(lhs_csig->ret, rhs_csig->ret) == 0))
                        ret = 0;
                    else {
                        ret = 1;
                        int i;
                        for (i = 0;i < lhs_num_args;i++) {
                            if (lhs_csig->args[i] != rhs_csig->args[i] &&
                                lily_sigequal(lhs_csig->args[i],
                                              rhs_csig->args[i]) == 0) {
                                ret = 0;
                                break;
                            }
                        }
                    }
                }
            }
            else
                ret = 1;
        }
        else
            ret = 0;
    }

    return ret;
}

/* lily_save_declared_method
   This is called when a method is closing, and it has a var representing
   another method var inside. */
void lily_save_declared_method(lily_symtab *symtab, lily_var *method_var)
{
    method_var->next = symtab->old_method_chain;
    symtab->old_method_chain = method_var;
}
