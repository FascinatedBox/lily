#include <string.h>

#include "lily_impl.h"
#include "lily_symtab.h"
#include "lily_value.h"

/* This creates the *_seed values. */
#include "lily_seed_symtab.h"

/** Symtab is responsible for:
    * Holding all classes, literals, vars, you name it.
    * Using the 'seeds' provided by lily_seed_symtab to initialize the starting
      symbols (__main__, literals 0 and 1, etc.).
    * On destruction, destroying all symbols.
    * Symtab currently handles all value derefs.
    * Hiding variables when they go out of scope (see lily_drop_block_vars)

    Notes:
    * Functions with 'try' in their name will return NULL or 0 on failure. They
      will never call lily_raise.
    * During symtab initialization, lily_raise cannot be called because the
      parser is not completely allocated and set.
**/

/*****************************************************************************/
/* Shared code                                                               */
/*****************************************************************************/

/*  shorthash_for_name
    This captures (up to) the first 8 bytes in a name. This is used for symbol
    comparisons before doing a strcmp to save time. */
static uint64_t shorthash_for_name(const char *name)
{
    const char *ch = &name[0];
    int i, shift;
    uint64_t ret;
    for (i = 0, shift = 0, ret = 0;
         *ch != '\0' && i != 8;
         ch++, i++, shift += 8) {
        ret |= ((uint64_t)*ch) << shift;
    }
    return ret;
}

/*  try_new_literal
    Attempt to add a new literal of the given class to the symtab. The literal
    will be created with the given value (copying it if it's a string).

    On success: A new literal is created and added to symtab's literals. For
                convenience, it's also returned.
    On failure: NULL is returned. */
static lily_literal *try_new_literal(lily_symtab *symtab, lily_class *cls,
        lily_raw_value value)
{
    lily_literal *lit = lily_malloc(sizeof(lily_literal));
    if (lit == NULL) {
        /* Make sure any string sent will be properly free'd. */
        if (cls->id == SYM_CLASS_STRING) {
            lily_string_val *sv = value.string;
            lily_free(sv->string);
            lily_free(sv);
        }
        return NULL;
    }
    /* Literals are either a string, integer, or number, so this is safe. */
    lit->sig = cls->sig;

    lit->flags = SYM_TYPE_LITERAL;
    lit->next = NULL;
    lit->value = value;
    /* Literals aren't put in registers, but they ARE put in a special vm
       table. This is the literal's index into that table. */
    lit->reg_spot = symtab->next_lit_spot;
    symtab->next_lit_spot++;

    if (symtab->lit_top == NULL)
        symtab->lit_start = lit;
    else
        symtab->lit_top->next = lit;

    symtab->lit_top = lit;

    return lit;
}

/*  lily_try_new_var
    Attempt to create a new var in the symtab that will have the given
    signature and name. The flags given are used to determine if the var is
    'readonly'. If it's readonly, it doesn't go into the vm's registers.

    On success: Returns a newly-created var that is automatically added to the
                symtab.
    On failure: NULL is returned. */
lily_var *lily_try_new_var(lily_symtab *symtab, lily_sig *sig, char *name,
        int flags)
{
    lily_var *var = lily_malloc(sizeof(lily_var));
    if (var == NULL)
        return NULL;

    var->name = lily_malloc(strlen(name) + 1);
    if (var->name == NULL) {
        lily_free(var);
        return NULL;
    }

    var->flags = VAL_IS_NIL | SYM_TYPE_VAR | flags;
    strcpy(var->name, name);
    var->line_num = *symtab->lex_linenum;

    var->shorthash = shorthash_for_name(name);
    var->sig = sig;
    var->next = NULL;
    var->parent = NULL;

    if ((flags & VAR_IS_READONLY) == 0) {
        var->reg_spot = symtab->next_register_spot;
        symtab->next_register_spot++;
        var->function_depth = symtab->function_depth;
    }
    else {
        /* Built-in and user-declared functions are both put into a table of
           functions. */
        var->reg_spot = symtab->next_function_spot;
        symtab->next_function_spot++;
        var->function_depth = -1;
    }

    if (symtab->var_start == NULL)
        symtab->var_start = var;
    else
        symtab->var_top->next = var;

    symtab->var_top = var;

    return var;
}

/*  get_template_max
    Recurse into a signature and determine the number of templates used. This
    is important for emitter, which needs to know how many sigs to blank before
    evaluating a call.

    sig:          The signature to check.
    template_max: This is a pointer set to the number of templates that the
                  given sig takes (template index + 1). This is 0 if the given
                  signature does not use templates. */
static void get_template_max(lily_sig *sig, int *template_max)
{
    /* function uses NULL at [1] to mean it takes no args, and NULL at [0] to
       mean that nothing is returned. */
    if (sig == NULL)
        return;

    if (sig->cls->id == SYM_CLASS_TEMPLATE) {
        if ((sig->template_pos + 1) > *template_max)
            *template_max = sig->template_pos + 1;
    }
    else if (sig->siglist) {
        int i;
        for (i = 0;i < sig->siglist_size;i++)
            get_template_max(sig->siglist[i], template_max);
    }
}

/*  lookup_sig
    Determine if the current signature exists in the symtab.

    Success: The signature from the symtab is returned.
    Failure: NULL is returned. */
static lily_sig *lookup_sig(lily_symtab *symtab, lily_sig *input_sig)
{
    lily_sig *iter_sig = symtab->root_sig;
    lily_sig *ret = NULL;

    /* This just means that input_sig was the last signature created. */
    if (iter_sig == input_sig)
        iter_sig = iter_sig->next;

    while (iter_sig) {
        if (iter_sig->cls == input_sig->cls) {
            if (iter_sig->siglist      != NULL &&
                iter_sig->siglist_size == input_sig->siglist_size &&
                iter_sig               != input_sig &&
                (iter_sig->flags & ~SIG_MAYBE_CIRCULAR) == input_sig->flags) {
                int i, match = 1;
                for (i = 0;i < iter_sig->siglist_size;i++) {
                    if (iter_sig->siglist[i] != input_sig->siglist[i]) {
                        match = 0;
                        break;
                    }
                }

                if (match == 1) {
                    ret = iter_sig;
                    break;
                }
            }
        }

        iter_sig = iter_sig->next;
    }

    return ret;
}

/*  finalize_sig
    Determine if the given signature is circular. Also, if its class is not the
    template class, determine how many templates the signature uses.

    The symtab doesn't use this information at all. These are convenience
    things for the emitter and the vm. */
static void finalize_sig(lily_sig *input_sig)
{
    if (input_sig->siglist) {
        /* functions are not containers, so circularity doesn't apply to them. */
        if (input_sig->cls->id != SYM_CLASS_FUNCTION) {
            int i;
            for (i = 0;i < input_sig->siglist_size;i++) {
                if (input_sig->siglist[i]->flags & SIG_MAYBE_CIRCULAR) {
                    input_sig->flags |= SIG_MAYBE_CIRCULAR;
                    break;
                }
            }
        }

        /* Find out the highest template index that this type has inside of it.
           For functions, this allows the emitter to reserve blank sigs for
           holding template matches. For other sigs, it allows the emitter to
           determine if a call result uses templates (since it has to be broken
           down if it does. */
        if (input_sig->cls->id != SYM_CLASS_TEMPLATE) {
            int max = 0;
            get_template_max(input_sig, &max);
            input_sig->template_pos = max;
        }
    }
}

/*  ensure_unique_sig
    This function is used by seed scanning to make sure that something with the
    same meaning as the given signature doesn't exist. This is a good thing,
    because it allows sig == sig comparisons (emitter and vm do this often).

    However, this function relies upon building a signature to check it. The
    problem with this is that it...trashes signatures if they're duplicates. So
    it's rather wasteful. This will go away when seed scanning goes away. */
static lily_sig *ensure_unique_sig(lily_symtab *symtab, lily_sig *input_sig)
{
    lily_sig *iter_sig = symtab->root_sig;
    lily_sig *previous_sig = NULL;
    int match = 0;

    /* This just means that input_sig was the last signature created. */
    if (iter_sig == input_sig)
        iter_sig = iter_sig->next;

    while (iter_sig) {
        if (iter_sig->cls == input_sig->cls) {
            if (iter_sig->siglist      != NULL &&
                iter_sig->siglist_size == input_sig->siglist_size &&
                iter_sig               != input_sig) {
                int i;
                match = 1;
                for (i = 0;i < iter_sig->siglist_size;i++) {
                    if (iter_sig->siglist[i] != input_sig->siglist[i]) {
                        match = 0;
                        break;
                    }
                }

                if (match == 1)
                    break;
            }
        }

        if (iter_sig->next == input_sig)
            previous_sig = iter_sig;

        iter_sig = iter_sig->next;
    }

    finalize_sig(input_sig);

    if (match) {
        /* Remove input_sig from the symtab's sig chain. */
        if (symtab->root_sig == input_sig)
            /* It is the root, so just advance the root. */
            symtab->root_sig = symtab->root_sig->next;
        else {
            /* Make the sig before it link to the node after it. This is
               theoretically safe because the chain goes from recent to least
               recent. So this should find the input signature before it finds
               one that equals it (and set previous_sig to something valid). */
            previous_sig->next = input_sig->next;
        }

        /* This is either NULL or something that only this sig uses. Don't free
           what's inside of the siglist though, since that's other signatures
           still in the chain. */
        lily_free(input_sig->siglist);
        lily_free(input_sig);

        input_sig = iter_sig;
    }

    return input_sig;
}

/*****************************************************************************/
/* 'Seed'-handling functions                                                 */
/*****************************************************************************/

/*  scan_seed_arg
    This takes a series of int's and uses them to define a new signature. This
    is currently only used by lily_cls_* files and builtin functions for
    defining function information. This also gets used to help create the sig
    for properties.

    This function will be destroyed soon. Passing a series of integers makes it
    impossible to have various modules imported by the interpreter (class 50
    could be a regexp or a database class). */
static lily_sig *scan_seed_arg(lily_symtab *symtab, const int *arg_ids,
        int *pos, int *ok)
{
    lily_sig *ret;
    int arg_id = arg_ids[*pos];
    int seed_pos = *pos + 1;
    *ok = 1;

    if (arg_id == -1)
        ret = NULL;
    else {
        lily_class *arg_class = lily_class_by_id(symtab, arg_id);
        if (arg_class->sig)
            ret = arg_class->sig;
        else {
            lily_sig *complex_sig = lily_try_sig_for_class(symtab, arg_class);
            lily_sig **siglist;
            int siglist_size;
            int flags = 0;

            if (arg_id == SYM_CLASS_TEMPLATE) {
                if (complex_sig)
                    complex_sig->template_pos = arg_ids[seed_pos];
                else
                    *ok = 0;

                seed_pos++;
                siglist = NULL;
                siglist_size = 0;
            }
            else {
                if (arg_class->template_count == -1) {
                    /* -1 means it takes a specified number of values. */
                    siglist_size = arg_ids[seed_pos];
                    seed_pos++;
                    /* Function needs flags in case the thing is varargs. */
                    if (arg_id == SYM_CLASS_FUNCTION) {
                        flags = arg_ids[seed_pos];
                        seed_pos++;
                    }
                }
                else {
                    siglist_size = arg_class->template_count;
                    flags = 0;
                }

                siglist = lily_malloc(siglist_size * sizeof(lily_sig *));
                if (siglist) {
                    int i;
                    for (i = 0;i < siglist_size;i++) {
                        siglist[i] = scan_seed_arg(symtab, arg_ids, &seed_pos,
                                ok);

                        if (*ok == 0)
                            break;
                    }

                    if (*ok == 0) {
                        /* This isn't tied to anything, so free it. Inner args
                           have already been ensured, so don't touch them. */
                        lily_free(siglist);
                        siglist = NULL;
                        *ok = 0;
                    }
                }
                else
                    *ok = 0;
            }

            if (*ok == 1 && complex_sig != NULL) {
                complex_sig->siglist = siglist;
                complex_sig->siglist_size = siglist_size;
                complex_sig->flags = flags;
                complex_sig = ensure_unique_sig(symtab, complex_sig);
                ret = complex_sig;
            }
            else
                ret = NULL;
        }
    }

    *pos = seed_pos;
    return ret;
}

/*  init_func_seed
    This uses scan_seed_arg to create a new function value. This is used to
    make space for new functions.

    On success: Returns the newly created var.
    On failure: Returns NULL. */
static lily_var *init_func_seed(lily_symtab *symtab,
        lily_class *cls, const lily_func_seed *seed)
{
    lily_var *ret = NULL;
    char *cls_name;
    if (cls)
        cls_name = cls->name;
    else
        cls_name = NULL;

    int ok = 1, pos = 0;
    lily_sig *new_sig = scan_seed_arg(symtab, seed->arg_ids, &pos, &ok);
    if (new_sig != NULL) {
        ret = lily_try_new_var(symtab, new_sig, seed->name, VAR_IS_READONLY);

        if (ret != NULL) {
            ret->parent = cls;
            ret->value.function = lily_try_new_foreign_function_val(seed->func,
                    cls_name, seed->name);

            if (ret->value.function != NULL)
                ret->flags &= ~(VAL_IS_NIL);
        }
    }

    return ret;
}

/*  init_prop_seeds
    This takes a series of "property seeds" for a given class and creates space
    in the class to hold those values. It also specifies where those values will
    be relative to the class.
    For example: Exception has two fields: a message, and traceback. The message
    will be at 0, and the traceback at 1. Complex classes are -really- just
    tuple's internally. */
static int init_prop_seeds(lily_symtab *symtab, lily_class *cls,
        const lily_prop_seed_t *seeds)
{
    const lily_prop_seed_t *seed_iter = seeds;
    lily_prop_entry *top = NULL;
    int ret = 1;
    int id = 0;

    do {
        int pos = 0, ok = 1;
        lily_sig *entry_sig = scan_seed_arg(symtab, seed_iter->prop_ids, &pos,
                &ok);
        lily_prop_entry *entry = lily_malloc(sizeof(lily_prop_entry));
        char *entry_name = lily_malloc(strlen(seed_iter->name) + 1);
        if (entry_sig == NULL || entry == NULL || entry_name == NULL) {
            /* Signatures are attached to symtab's root_sig when they get made,
               so there's no teardown for the sig necessary. */
            lily_free(entry);
            lily_free(entry_name);
            ret = 0;
            break;
        }
        strcpy(entry_name, seed_iter->name);
        entry->id = id;
        entry->name = entry_name;
        entry->sig = entry_sig;
        entry->name_shorthash = shorthash_for_name(entry_name);
        entry->next = NULL;
        if (top == NULL) {
            cls->properties = entry;
            top = entry;
        }
        else
            top->next = entry;

        id++;
        seed_iter = seed_iter->next;
    } while (seed_iter);

    return ret;
}

/*****************************************************************************/
/* Symtab initialization */
/*****************************************************************************/

/*  call_setups
    Symtab init, stage 6
    This calls the setup func of any class that has one. This is reponsible for
    setting up the seed_table of the class, and possibly more in the future. */
static int call_class_setups(lily_symtab *symtab)
{
    int ret = 1;
    lily_class *class_iter = symtab->class_chain;
    while (class_iter) {
        if (class_iter->setup_func &&
            class_iter->setup_func(class_iter) == 0) {
            ret = 0;
            break;
        }

        class_iter = class_iter->next;
    }

    return ret;
}

/*  read_global_seeds
    Symtab init, stage 5
    This initializes Lily's builtin functions through init_func_seed which
    automatically adds the seeds to var_top where they should be (since they're
    globals).
    All global functions are always loaded because the seeds don't have a
    shorthash, and seeds can't be modified either. So lily_var_by_name would
    end up doing a lot of unnecessary name comparisons for each new var. */
static int read_global_seeds(lily_symtab *symtab)
{
    int ret;
    const lily_func_seed *seed_iter;

    ret = 1;

    for (seed_iter = &GLOBAL_SEED_START;
         seed_iter != NULL;
         seed_iter = seed_iter->next) {

        if (init_func_seed(symtab, NULL, seed_iter) == NULL) {
            ret = 0;
            break;
        }
    }

    return ret;
}

/*  init_lily_main
    Symtab init, stage 4
    This creates __main__, which holds all code that is not explicitly put
    inside of a Lily function. */
static int init_lily_main(lily_symtab *symtab)
{
    lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_FUNCTION);
    lily_sig *new_sig = lily_try_sig_for_class(symtab, cls);
    if (new_sig == NULL)
        return 0;

    new_sig->siglist = lily_malloc(2 * sizeof(lily_sig));
    if (new_sig->siglist == NULL)
        return 0;

    new_sig->siglist[0] = NULL;
    new_sig->siglist_size = 1;
    new_sig->template_pos = 0;
    new_sig->flags = 0;

    lily_var *var = lily_try_new_var(symtab, new_sig, "__main__", 0);

    if (var == NULL)
        return 0;

    /* The emitter will mark __main__ as non-nil when it's entered. Until then,
       leave it alone because it doesn't have a value. */

    return 1;
}

/*  init_literals
    Symtab init, stage 3
    This creates literal values for 0 and 1. This is done for and/or handling
    in emitter. */
static int init_literals(lily_symtab *symtab)
{
    int i, ret;
    lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_INTEGER);
    lily_literal *lit;
    ret = 1;
    lily_raw_value raw;

    for (i = 0;i < 2;i++) {
        raw.integer = i;
        lit = try_new_literal(symtab, cls, raw);
        if (lit == NULL)
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

    class_count = sizeof(class_seeds) / sizeof(class_seeds[0]);
    ret = 1;

    for (i = 0;i < class_count;i++) {
        lily_class *new_class = lily_malloc(sizeof(lily_class));

        if (new_class != NULL) {
            lily_sig *sig;

            /* If a class doesn't take templates (or isn't template), then
               it can have a default sig that lily_try_sig_for_class can yield.
               This saves memory, and is necessary now that sig comparison is
               by pointer. */
            if (class_seeds[i].template_count != 0 ||
                i == SYM_CLASS_TEMPLATE) {
                sig = NULL;
            }
            else {
                /* A basic class? Make a quick default sig for it. */
                sig = lily_malloc(sizeof(lily_sig));
                if (sig != NULL) {
                    sig->cls = new_class;
                    /* Make sure this is null so any attempt to free it won't
                       cause a problem. */
                    sig->siglist = NULL;
                    sig->siglist_size = 0;
                    sig->flags = 0;
                    /* Non-template signatures use this to mean that this sig
                       does not have templates inside. */
                    sig->template_pos = 0;
                    if (i == SYM_CLASS_ANY)
                        sig->flags |= SIG_MAYBE_CIRCULAR;

                    sig->next = symtab->root_sig;
                    symtab->root_sig = sig;
                }
                else
                    ret = 0;
            }

            new_class->name = class_seeds[i].name;
            new_class->call_start = NULL;
            new_class->call_top = NULL;
            new_class->sig = sig;
            new_class->id = i;
            new_class->template_count = class_seeds[i].template_count;
            new_class->shorthash = shorthash_for_name(new_class->name);
            new_class->gc_marker = class_seeds[i].gc_marker;
            new_class->flags = class_seeds[i].flags;
            new_class->is_refcounted = class_seeds[i].is_refcounted;
            new_class->seed_table = NULL;
            new_class->setup_func = class_seeds[i].setup_func;
            new_class->eq_func = class_seeds[i].eq_func;
            new_class->properties = NULL;
            new_class->prop_start = 0;
            new_class->parent = NULL;

            new_class->next = symtab->class_chain;
            symtab->class_chain = new_class;

            if (ret && class_seeds[i].prop_seeds != NULL)
                ret = init_prop_seeds(symtab, new_class,
                        class_seeds[i].prop_seeds);
        }
        else
            ret = 0;

        if (ret == 0)
            break;
    }

    if (ret == 1) {
        /* No direct declaration of packages (for now?) */
        lily_class *package_cls = lily_class_by_id(symtab, SYM_CLASS_PACKAGE);
        package_cls->shorthash = 0;

        /* Now that all classes are established, fix class parents to their
           real values. */
        lily_class *class_iter;
        for (class_iter = symtab->class_chain, i = 0;
             class_iter;
             class_iter = class_iter->next, i++) {
            if (class_seeds[i].parent_name != NULL) {
                class_iter->parent = lily_class_by_name(symtab,
                        class_seeds[i].parent_name);
            }
        }
    }

    return ret;
}

/*  lily_new_symtab:
    Symtab init, stage 1
    This creates a new symtab, then calls the init stages in order.

    On success: The newly-created symtab is returned.
    On failure: NULL is returned. */
lily_symtab *lily_new_symtab(lily_raiser *raiser)
{
    lily_symtab *symtab = lily_malloc(sizeof(lily_symtab));
    uint16_t v = 0;

    if (symtab == NULL)
        return NULL;

    symtab->next_register_spot = 0;
    symtab->next_lit_spot = 0;
    symtab->next_function_spot = 0;
    symtab->var_start = NULL;
    symtab->var_top = NULL;
    symtab->old_function_chain = NULL;
    symtab->class_chain = NULL;
    symtab->lit_start = NULL;
    symtab->lit_top = NULL;
    symtab->function_depth = 1;
    /* lily_try_new_var expects lex_linenum to be the lexer's line number.
       0 is used, because these are all builtins, and the lexer may have failed
       to initialize anyway. */
    symtab->lex_linenum = &v;
    symtab->root_sig = NULL;

    if (!init_classes(symtab) || !init_literals(symtab) ||
        !init_lily_main(symtab) ||
        !read_global_seeds(symtab) ||
        !call_class_setups(symtab)) {
        /* First the literals and vars created, if any... */
        lily_free_symtab_lits_and_vars(symtab);
        /* then delete the symtab. */
        lily_free_symtab(symtab);
        return NULL;
    }

    symtab->raiser = raiser;

    return symtab;
}

/*****************************************************************************/
/* Symtab teardown                                                           */
/*****************************************************************************/

/** Symtab free-ing **/
/*  free_vars
    Given a chain of vars, free the ones that are not marked nil. Most symtab
    vars don't get values, but a few special ones (like the sys package and
    __main__) have values. */
void free_vars(lily_var *var)
{
    lily_var *var_temp;

    while (var != NULL) {
        var_temp = var->next;
        if ((var->flags & VAL_IS_NIL) == 0) {
            int cls_id = var->sig->cls->id;
            if (cls_id == SYM_CLASS_FUNCTION)
                lily_deref_function_val(var->value.function);
            else
                lily_deref_unknown_raw_val(var->sig, var->value);
        }
        lily_free(var->name);
        lily_free(var);

        var = var_temp;
    }
}

/*  free_properties
    Free property information associated with a given class. */
static void free_properties(lily_class *cls)
{
    lily_prop_entry *prop_iter = cls->properties;
    lily_prop_entry *next_prop;
    while (prop_iter) {
        next_prop = prop_iter->next;

        lily_free(prop_iter->name);
        lily_free(prop_iter);

        prop_iter = next_prop;
    }
}

/*  free_lily_main
    Regular function teardown can't be done on __main__ because __main__ does
    not keep a copy of function names. So it uses this. */
static void free_lily_main(lily_function_val *fv)
{
    lily_free(fv->reg_info);
    lily_free(fv->code);
    lily_free(fv);
}

/*  lily_free_symtab_lits_and_vars

    This frees all literals and vars within the symtab. This is the first step
    to tearing down the symtab, with the second being to call lily_free_symtab.

    Symtab's teardown is in two steps so that the gc can have one final pass
    after the vars get a deref. This allows the gc to attempt cleanly destroying
    all values. It needs signature and class info, which is why that IS NOT
    touched here.

    Additionally, parts of symtab init may have failed, so NULL checks are
    important.

    symtab: The symtab to delete the vars and literals of. */
void lily_free_symtab_lits_and_vars(lily_symtab *symtab)
{
    lily_literal *lit, *lit_temp;

    lit = symtab->lit_start;

    while (lit != NULL) {
        lit_temp = lit->next;

        if (lit->sig->cls->is_refcounted)
            lily_deref_string_val(lit->value.string);

        lily_free(lit);

        lit = lit_temp;
    }

    /* This should be okay, because nothing will want to use the vars at this
       point. */
    lily_class *class_iter = symtab->class_chain;
    while (class_iter) {
        if (class_iter->properties != NULL)
            free_properties(class_iter);

        if (class_iter->call_start != NULL)
            free_vars(class_iter->call_start);

        class_iter = class_iter->next;
    }

    lily_function_val *main_function;

    if (symtab->var_start &&
        ((symtab->var_start->flags & VAL_IS_NIL) == 0))
        main_function = symtab->var_start->value.function;
    else
        main_function = NULL;

    if (symtab->var_start != NULL)
        free_vars(symtab->var_start);
    if (symtab->old_function_chain != NULL)
        free_vars(symtab->old_function_chain);

    if (main_function != NULL)
        free_lily_main(main_function);
}

/*  lily_free_symtab

    This destroys the classes and signatures stored in the symtab, as well as
    the symtab itself. This is called after the vm has had a chance to tell the
    gc to do a final sweep (where type info is necessary).

    symtab: The symtab to destroy the vars of. */
void lily_free_symtab(lily_symtab *symtab)
{
    lily_sig *sig, *sig_temp;

    /* Destroy the signatures before the classes, since the sigs need to check
       the class id to make sure there isn't a call sig to destroy. */
    sig = symtab->root_sig;
    int j = 0;
    while (sig != NULL) {
        j++;
        sig_temp = sig->next;

        /* The siglist is either NULL or set to something that needs to be
           deleted. */
        lily_free(sig->siglist);
        lily_free(sig);
        sig = sig_temp;
    }

    lily_class *class_iter = symtab->class_chain;
    while (class_iter) {
        lily_class *class_next = class_iter->next;
        lily_free(class_iter);
        class_iter = class_next;
    }

    lily_free(symtab);
}

/*****************************************************************************/
/* Exported function                                                         */
/*****************************************************************************/

/* These next three are used to get an integer, double, or string literal.
   They first look to see of the symtab has a literal with that value, then
   attempt to create it if there isn't one. */

lily_literal *lily_get_integer_literal(lily_symtab *symtab, int64_t int_val)
{
    lily_literal *lit, *ret;
    ret = NULL;
    lily_class *integer_cls = lily_class_by_id(symtab, SYM_CLASS_INTEGER);
    lily_sig *want_sig = integer_cls->sig;

    for (lit = symtab->lit_start;lit != NULL;lit = lit->next) {
        if (lit->sig == want_sig && lit->value.integer == int_val) {
            ret = lit;
            break;
        }
    }

    if (ret == NULL) {
        lily_raw_value v = {.integer = int_val};
        ret = try_new_literal(symtab, integer_cls, v);
        if (ret == NULL)
            lily_raise_nomem(symtab->raiser);

        ret->value = v;
    }

    return ret;
}

lily_literal *lily_get_double_literal(lily_symtab *symtab, double dbl_val)
{
    lily_literal *lit, *ret;
    ret = NULL;
    lily_class *double_cls = lily_class_by_id(symtab, SYM_CLASS_DOUBLE);
    lily_sig *want_sig = double_cls->sig;

    for (lit = symtab->lit_start;lit != NULL;lit = lit->next) {
        if (lit->sig == want_sig && lit->value.doubleval == dbl_val) {
            ret = lit;
            break;
        }
    }

    if (ret == NULL) {
        lily_raw_value v = {.doubleval = dbl_val};
        ret = try_new_literal(symtab, double_cls, v);
        if (ret == NULL)
            lily_raise_nomem(symtab->raiser);

        ret->value = v;
    }

    return ret;
}

lily_literal *lily_get_string_literal(lily_symtab *symtab, char *want_string)
{
    lily_literal *lit, *ret;
    ret = NULL;
    int want_string_len = strlen(want_string);

    for (lit = symtab->lit_start;lit;lit = lit->next) {
        if (lit->sig->cls->id == SYM_CLASS_STRING) {
            if (lit->value.string->size == want_string_len &&
                strcmp(lit->value.string->string, want_string) == 0) {
                ret = lit;
                break;
            }
        }
    }

    if (ret == NULL) {
        lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_STRING);
        char *string_buffer = lily_malloc((want_string_len + 1) * sizeof(char));
        lily_string_val *sv = lily_malloc(sizeof(lily_string_val));
        if (sv == NULL || string_buffer == NULL) {
            lily_free(sv);
            lily_free(string_buffer);
            lily_raise_nomem(symtab->raiser);
        }

        strcpy(string_buffer, want_string);
        sv->string = string_buffer;
        sv->size = want_string_len;
        sv->refcount = 1;

        lily_raw_value v;
        v.string = sv;
        ret = try_new_literal(symtab, cls, v);
        if (ret == NULL)
            lily_raise_nomem(symtab->raiser);
    }

    return ret;
}

/*  lily_try_sig_for_class
    Attempt to get the default signature of the given class. If the given class
    doesn't have a default signature (because it takes templates), then create
    a new signature without a siglist and return that. */
lily_sig *lily_try_sig_for_class(lily_symtab *symtab, lily_class *cls)
{
    lily_sig *sig;

    /* init_classes doesn't make a default sig for classes that need complex
       sigs. This works so long as init_classes works right. */
    if (cls->sig == NULL) {
        sig = lily_malloc(sizeof(lily_sig));
        if (sig != NULL) {
            sig->cls = cls;
            sig->siglist = NULL;
            sig->siglist_size = 0;
            sig->flags = 0;

            sig->next = symtab->root_sig;
            symtab->root_sig = sig;
        }
    }
    else
        sig = cls->sig;

    return sig;
}

lily_class *lily_class_by_id(lily_symtab *symtab, int class_id)
{
    lily_class *class_iter = symtab->class_chain;
    while (class_iter) {
        if (class_iter->id == class_id)
            break;

        class_iter = class_iter->next;
    }

    return class_iter;
}

/*  lily_class_by_name
    Try to find a class from a given non-NULL name.
    On success: The class is returned.
    On failure: NULL is returned. */
lily_class *lily_class_by_name(lily_symtab *symtab, const char *name)
{
    uint64_t shorthash = shorthash_for_name(name);
    lily_class *class_iter = symtab->class_chain;
    while (class_iter) {
        if (class_iter->shorthash == shorthash &&
            strcmp(class_iter->name, name) == 0)
            break;

        class_iter = class_iter->next;
    }

    return class_iter;
}

/*  lily_find_class_callable
    Check if a class has a given function within it. If it doesn't, see if the
    class comes with a 'seed_table' that defines more functions. If it has a
    seed table, attempt to do a dynamic load of the given function.

    This is a bit complicated, but it saves a LOT of memory from not having to
    make signature+var information for every builtin thing. */
lily_var *lily_find_class_callable(lily_symtab *symtab, lily_class *cls,
        char *name)
{
    lily_var *iter;
    uint64_t shorthash = shorthash_for_name(name);

    for (iter = cls->call_start;iter != NULL;iter = iter->next) {
        if (iter->shorthash == shorthash && strcmp(iter->name, name) == 0)
            break;
    }

    /* Maybe it's something that hasn't been loaded in the symtab yet. */
    if (iter == NULL && cls->seed_table != NULL) {
        const lily_func_seed *seed = cls->seed_table;
        while (seed) {
            if (strcmp(seed->name, name) == 0) {
                lily_var *save_top = symtab->var_top;

                iter = init_func_seed(symtab, cls, seed);
                if (iter == NULL)
                    lily_raise_nomem(symtab->raiser);
                else {
                    /* The new var is added to symtab's vars. Take it off of
                       there since this var shouldn't be globally reachable.
                       __main__ is the first var, so this shouldn't have to
                       check for var_top == var_start. */
                    if (cls->call_start == NULL)
                        cls->call_start = iter;

                    if (cls->call_top != NULL)
                        cls->call_top->next = iter;

                    cls->call_top = iter;

                    /* This is a builtin, so fix the line number to 0. */
                    iter->line_num = 0;
                    symtab->var_top = save_top;
                    symtab->var_top->next = NULL;
                }
                break;
            }
            seed = seed->next;
        }
    }

    return iter;
}

int lily_keyword_by_name(char *name)
{
    int i;
    uint64_t shorthash = shorthash_for_name(name);

    for (i = 0;i <= KEY_LAST_ID;i++) {
        if (keywords[i].shorthash == shorthash &&
            strcmp(keywords[i].name, name) == 0)
            return i;
    }

    return -1;
}

/*  lily_scoped_var_by_name
    Do a var lookup but start from the given var. This is used for looking up
    values within a package. Returns the var wanted or NULL. */
lily_var *lily_scoped_var_by_name(lily_symtab *symtab, lily_var *scope_chain,
        char *name)
{
    lily_var *var = scope_chain;
    uint64_t shorthash = shorthash_for_name(name);

    while (var != NULL) {
        if (var->shorthash == shorthash &&
            ((var->flags & SYM_OUT_OF_SCOPE) == 0) &&
            strcmp(var->name, name) == 0)
            return var;
        var = var->next;
    }

    return NULL;
}

lily_var *lily_var_by_name(lily_symtab *symtab, char *name)
{
    lily_var *var = symtab->var_start;
    uint64_t shorthash = shorthash_for_name(name);

    while (var != NULL) {
        if (var->shorthash == shorthash &&
            ((var->flags & SYM_OUT_OF_SCOPE) == 0) &&
            strcmp(var->name, name) == 0)
            return var;
        var = var->next;
    }

    return NULL;
}

/*  lily_hide_block_vars
    This function is called by emitter when a block goes out of scope. Vars
    after 'start' are now out of scope. But...don't delete them because the
    emitter will need to know their type info later. */
void lily_hide_block_vars(lily_symtab *symtab, lily_var *start)
{
    start = start->next;

    while (start) {
        start->flags |= SYM_OUT_OF_SCOPE;
        start = start->next;
    }
}

/*  lily_try_sig_from_ids
    This is used by the apache module to create a signature in a less-awful
    way than doing it manually. This unfortunately uses the really awful seed
    scanning functions.
    In the future, there will be something to get a signature from a string. */
lily_sig *lily_try_sig_from_ids(lily_symtab *symtab, const int *ids)
{
    int pos = 0, ok = 1;
    return scan_seed_arg(symtab, ids, &pos, &ok);
}

/*  lily_build_ensure_sig
    This function is used to ensure that creating a signature for 'cls' with
    the given information will not result in a duplicate signature entry.
    Unique signatures are a good thing, because that allows sig == sig
    comparisons by emitter and the vm.
    This creates a new signature if, and only if, it would be unique.

    cls:            The base class to look for.
    flags:          Flags for the signature. Important for functions, which
                    may/may not be SIG_IS_VARARGS.
    siglist:        The siglist that proper signatures will be pulled from.
    offset:         In siglist, where to start taking signatures.
    entries_to_use: How many signatures to take after 'offset'.

    This is used by parser and emitter to make sure they don't create
    signatures they'll have to throw away.

    This raises NoMemoryError if it needs to make a sig and can't. Otherwise,
    a unique, valid signature is always returned. */
lily_sig *lily_build_ensure_sig(lily_symtab *symtab, lily_class *cls,
        int flags, lily_sig **siglist, int offset, int entries_to_use)
{
    lily_sig fake_sig;

    fake_sig.cls = cls;
    fake_sig.template_pos = 0;
    fake_sig.siglist = siglist + offset;
    fake_sig.siglist_size = entries_to_use;
    fake_sig.flags = flags;
    fake_sig.next = NULL;

    /* The reason it's done like this is purely to save memory. There's no
       point in creating a new signature if it already exists (since that just
       means the new one has to be destroyed). */
    lily_sig *result_sig = lookup_sig(symtab, &fake_sig);
    if (result_sig == NULL) {
        lily_sig *new_sig = lily_malloc(sizeof(lily_sig));
        lily_sig **new_siglist = lily_malloc(entries_to_use *
                sizeof(lily_sig *));

        if (new_sig == NULL || new_siglist == NULL) {
            lily_free(new_sig);
            lily_free(new_siglist);
            lily_raise_nomem(symtab->raiser);
        }

        memcpy(new_sig, &fake_sig, sizeof(lily_sig));
        memcpy(new_siglist, siglist + offset, sizeof(lily_sig *) * entries_to_use);
        new_sig->siglist = new_siglist;
        new_sig->siglist_size = entries_to_use;

        new_sig->next = symtab->root_sig;
        symtab->root_sig = new_sig;

        finalize_sig(new_sig);
        result_sig = new_sig;
    }

    return result_sig;
}

/*  lily_check_right_inherits_or_is
    Check if 'right' is the same class as 'left' or inherits from it. This
    function has a specific name so that the parameters won't get accidentally
    swapped at some point in the future. */
int lily_check_right_inherits_or_is(lily_class *left, lily_class *right)
{
    int ret = 0;
    if (left != right) {
        while (right != NULL) {
            right = right->parent;
            if (right == left) {
                ret = 1;
                break;
            }
        }
    }
    else
        ret = 1;

    return ret;
}
