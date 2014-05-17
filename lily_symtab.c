#include <string.h>

#include "lily_impl.h"
#include "lily_symtab.h"
#include "lily_value.h"

/* This creates the *_seed values. */
#include "lily_seed_symtab.h"
#include "lily_pkg_str.h"
#include "lily_pkg_list.h"
#include "lily_vm.h"

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
    * A shorthash is a uint64 value that holds the integer value of up to the
      first 8 bytes of a string. The lexer generates these for doing any kind of
      lookup.
**/

/** Symtab init helpers, and shared code **/
/* lily_try_new_var
   This creates a new var using the signature given, and copying the name.
   It is okay to pass a sig without list element/call info, since
   lily_try_sig_for_class ensures that important parts are set to NULL.
   This function will add the var to the symtab on success.
   Note: 'try' means this call returns NULL on failure. */
lily_var *lily_try_new_var(lily_symtab *symtab, lily_sig *sig, char *name,
        uint64_t shorthash, int flags)
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

    var->shorthash = shorthash;
    var->sig = sig;
    var->next = NULL;

    if ((flags & VAR_IS_READONLY) == 0) {
        var->reg_spot = symtab->next_register_spot;
        symtab->next_register_spot++;
        var->method_depth = symtab->method_depth;
    }
    else {
        /* Vars that are never intended to be assigned to (such as builtin
           functions and declared methods) are not placed in a register. */
        var->reg_spot = -1;
        var->method_depth = -1;
    }

    if (symtab->var_start == NULL)
        symtab->var_start = var;
    else
        symtab->var_top->next = var;

    symtab->var_top = var;

    return var;
}

/* scan_seed_arg
   This takes a seed that defines a signature and creates the appropriate sig
   for it. This is able to handle complex signatures nested inside of each
   other. */
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
                if (arg_id == SYM_CLASS_METHOD ||
                    arg_id == SYM_CLASS_FUNCTION) {
                    siglist_size = arg_ids[seed_pos];
                    seed_pos++;
                    flags = arg_ids[seed_pos];
                    seed_pos++;
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
                complex_sig = lily_ensure_unique_sig(symtab, complex_sig);
                ret = complex_sig;
            }
            else
                ret = NULL;
        }
    }

    *pos = seed_pos;
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
    ret = 1;
    char shorthash_buf[8];

    for (i = 0;i < seed_count;i++) {
        lily_func_seed *seed = seeds[i];

        /* This zaps all of shorthash_buf because of the cast. This is done so
           that previous uses don't affect the hash of the rest. */
        *((uint64_t *)shorthash_buf) = 0;
        strncpy(shorthash_buf, seed->name, 8);

        uint64_t shorthash = *(uint64_t *)shorthash_buf;
        int ok = 1, pos = 0;
        lily_sig *new_sig = scan_seed_arg(symtab, seed->arg_ids, &pos, &ok);
        if (new_sig != NULL) {
            lily_var *var = lily_try_new_var(symtab, new_sig, seed->name,
                    shorthash, 0);

            if (var != NULL) {
                var->value.function = lily_try_new_function_val(seed->func, 
                        seed->name);

                if (var->value.function != NULL)
                    var->flags &= ~(VAL_IS_NIL);
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
        cls->call_start = save_top->next;
        cls->call_top = symtab->var_top;
        symtab->var_top = save_top;
        save_top->next = NULL;
    }

    return ret;
}

/* init_lily_main
   Symtab init, stage 4
   This creates __main__, which is a hidden method that holds all code that is
   not put inside of a lily method. This is outside of read_seeds since it's the
   only builtin method. It also takes no args.
   __main__ is always the first var, and thus can always be found at the
   symtab's var_start. */
static int init_lily_main(lily_symtab *symtab)
{
    lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_METHOD);
    lily_sig *new_sig = lily_try_sig_for_class(symtab, cls);
    if (new_sig == NULL)
        return 0;

    new_sig->siglist = lily_malloc(2 * sizeof(lily_sig));
    if (new_sig->siglist == NULL)
        return 0;

    new_sig->siglist[0] = NULL;
    new_sig->siglist[1] = NULL;
    new_sig->siglist_size = 2;
    new_sig->flags = 0;

    lily_var *var = lily_try_new_var(symtab, new_sig, "__main__",
            6872332955275845471, 0);

    if (var == NULL)
        return 0;

    /* The emitter will mark __main__ as non-nil when it's entered. Until then,
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
    char shorthash_buf[8];

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

            if (i == SYM_CLASS_METHOD ||
                i == SYM_CLASS_LIST ||
                i == SYM_CLASS_FUNCTION ||
                i == SYM_CLASS_TEMPLATE ||
                i == SYM_CLASS_HASH) {
                /* lily_try_sig_for_class will always yield a new signature when
                   these are given. So these classes do not need a default
                   signature. */
                sig = NULL;
            }
            else {
                /* The signatures for everything else (integer, number, etc.)
                   never have inner elements that are modified. So this creates
                   a simple sig for lily_try_sig_for_class to return. */
                sig = lily_malloc(sizeof(lily_sig));
                if (sig != NULL) {
                    sig->cls = new_class;
                    /* Make sure this is null so any attempt to free it won't
                       cause a problem. */
                    sig->siglist = NULL;
                    sig->siglist_size = 0;
                    sig->flags = 0;
                    if (i == SYM_CLASS_OBJECT)
                        sig->flags |= SIG_MAYBE_CIRCULAR;

                    sig->next = symtab->root_sig;
                    symtab->root_sig = sig;
                }
                else
                    ret = 0;
            }

            new_class->name = class_seeds[i].name;
            /* This zaps all of shorthash_buf because of the cast. This is done
               so that previous uses don't affect the hash of the rest. */
            *((uint64_t *)shorthash_buf) = 0;
            strncpy(shorthash_buf, new_class->name, 8);

            new_class->call_start = NULL;
            new_class->call_top = NULL;
            new_class->sig = sig;
            new_class->id = i;
            new_class->template_count = class_seeds[i].template_count;
            new_class->shorthash = *(uint64_t *)shorthash_buf;
            new_class->gc_marker = class_seeds[i].gc_marker;
            new_class->flags = class_seeds[i].flags;
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
        !init_lily_main(symtab) ||
        !read_seeds(symtab, builtin_seeds, NUM_BUILTIN_SEEDS) ||
        !init_package(symtab, SYM_CLASS_LIST, list_seeds, NUM_LIST_SEEDS) ||
        !init_package(symtab, SYM_CLASS_STR, str_seeds, NUM_STR_SEEDS)) {
        /* First the literals and vars created, if any... */
        lily_free_symtab_lits_and_vars(symtab);
        /* then delete the symtab. */
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
        if ((var->flags & VAL_IS_NIL) == 0) {
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

static void free_lily_main(lily_method_val *mv)
{
    lily_free(mv->reg_info);
    lily_free(mv->code);
    lily_free(mv);
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
            lily_deref_str_val(lit->value.str);

        lily_free(lit);

        lit = lit_temp;
    }

    /* This should be okay, because nothing will want to use the vars at this
       point. */
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
        ((symtab->var_start->flags & VAL_IS_NIL) == 0))
        main_method = symtab->var_start->value.method;
    else
        main_method = NULL;

    if (symtab->var_start != NULL)
        free_vars(symtab->var_start);
    if (symtab->old_method_chain != NULL)
        free_vars(symtab->old_method_chain);

    if (main_method != NULL)
        free_lily_main(main_method);
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
lily_literal *lily_get_intnum_literal(lily_symtab *symtab, lily_class *cls,
        lily_raw_value want_value)
{
    lily_literal *lit, *ret;
    ret = NULL;
    lily_sig *want_sig = cls->sig;

    for (lit = symtab->lit_start;lit != NULL;lit = lit->next) {
        if (lit->sig == want_sig) {
            if ((cls->id == SYM_CLASS_INTEGER &&
                 lit->value.integer == want_value.integer) ||
                (cls->id == SYM_CLASS_NUMBER &&
                 lit->value.number == want_value.number))
                break;
        }
    }

    if (ret == NULL) {
        lily_raw_value v;
        if (cls->id == SYM_CLASS_INTEGER)
            v.integer = want_value.integer;
        else
            v.number = want_value.number;

        /* lily_new_literal is guaranteed to work or raise nomem, so this is
           safe. */
        ret = lily_new_literal(symtab, cls, v);
        ret->value = v;
    }

    return ret;
}

lily_literal *lily_get_str_literal(lily_symtab *symtab, char *want_str)
{
    /* The length is given because this can come from a user-defined string, or
       from something like __file__ or __method__.
       In the first case, the user may have added \0's, which is why a size
       requirement was added. */
    lily_literal *lit, *ret;
    ret = NULL;
    int want_str_len = strlen(want_str);

    for (lit = symtab->lit_start;lit;lit = lit->next) {
        if (lit->sig->cls->id == SYM_CLASS_STR) {
            if (lit->value.str->size == want_str_len &&
                strcmp(lit->value.str->str, want_str) == 0) {
                ret = lit;
                break;
            }
        }
    }

    if (ret == NULL) {
        lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_STR);
        /* lily_new_literal is guaranteed to work or raise nomem, so this is
           safe. */
        char *str_buffer = lily_malloc((want_str_len + 1) * sizeof(char));
        lily_str_val *sv = lily_malloc(sizeof(lily_str_val));
        if (sv == NULL || str_buffer == NULL) {
            lily_free(sv);
            lily_free(str_buffer);
            lily_raise_nomem(symtab->raiser);
        }

        strcpy(str_buffer, want_str);
        sv->str = str_buffer;
        sv->size = want_str_len;
        sv->refcount = 1;

        lily_raw_value v;
        v.str = sv;
        ret = lily_new_literal(symtab, cls, v);
    }

    return ret;
}

/* try_sig_for_class
   If the given class does not require extra data (like how lists need an inner
   element, calls need args, etc), then this will return the shared signature
   of a class. This won't fail.
   If the signature will require complex data, an attempt is made at allocating
   a new signature. If this allocation fails, NULL is returned. */
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

/* lily_class_by_id
   This function will return a class for a particular class id. This is
   typically used to quickly fetch builtin classes. */
lily_class *lily_class_by_id(lily_symtab *symtab, int class_id)
{
    return symtab->classes[class_id];
}

/* lily_class_by_name
   This function returns a class for a given shorthash, or NULL. This doesn't
   take a name, because all class names are <= 8 bytes in name length. */
lily_class *lily_class_by_hash(lily_symtab *symtab, uint64_t shorthash)
{
    int i;
    lily_class **classes = symtab->classes;

    for (i = 0;i <= symtab->class_pos;i++) {
        if (classes[i]->shorthash == shorthash)
            return classes[i];
    }

    return NULL;
}

/* lily_find_class_callable
   This function will see if a given clas has a function with the given name.
   NULL is returned on failure. */
lily_var *lily_find_class_callable(lily_class *cls, char *name,
        uint64_t shorthash)
{
    lily_var *iter;

    for (iter = cls->call_start;iter != NULL;iter = iter->next) {
        if (iter->shorthash == shorthash && strcmp(iter->name, name) == 0)
            break;
    }

    return iter;
}

/* lily_keyword_by_name
   Attempt to lookup a keyword based on 64-bit short hash, then on a name if
   necessary. Keywords are in a static list, (via lily_seed_symtab.h), so this
   doesn't require a symtab. */
int lily_keyword_by_name(char *name, uint64_t shorthash)
{
    int i;
    for (i = 0;i <= KEY_LAST_ID;i++) {
        if (keywords[i].shorthash == shorthash &&
            strcmp(keywords[i].name, name) == 0)
            return i;
    }

    return -1;
}

/* lily_var_by_name
   Search the symtab for a var with a name of 'name'. This will return the var
   or NULL. */
lily_var *lily_var_by_name(lily_symtab *symtab, char *name, uint64_t shorthash)
{
    lily_var *var = symtab->var_start;

    while (var != NULL) {
        if (var->shorthash == shorthash &&
            ((var->flags & SYM_OUT_OF_SCOPE) == 0) &&
            strcmp(var->name, name) == 0)
            return var;
        var = var->next;
    }

    return NULL;
}

/* lily_new_literal
   This adds a new literal to the given symtab. The literal will be of the class
   'cls', and be given the value 'value'. The symbol created does not have
   VAL_IS_NIL set, because the literal is assumed to never be nil.
   This function currently handles only integer, number, and str values.
   Warning: This function calls lily_raise_nomem instead of returning NULL. */
lily_literal *lily_new_literal(lily_symtab *symtab, lily_class *cls,
        lily_raw_value value)
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

/* lily_save_declared_method
   This is called when a method is closing, and it has a var representing
   another method var inside. */
void lily_save_declared_method(lily_symtab *symtab, lily_var *method_var)
{
    method_var->next = symtab->old_method_chain;
    symtab->old_method_chain = method_var;
}

/* lily_ensure_unique_sig
   This looks through the symtab's current signatures to see if any describe the
   same thing as the given signature.
   * If a match is not found, the input_sig is returned.
   * If a match is found, input_sig is destroyed and removed from symtab's sig
     chain. The matching signature is returned. Because of this, the return of
     this function must NEVER be ignored, unless no var is currently using
     input_sig.
   As the name suggests, this ensures that each signature describes a unique
   thing.
   It is expected that inner signatures will be ensured before outer signatures
   are: a list[list[list[list[integer]]]] first checking that list[integer] is
   unique, then list[list[integer]] is unique, and so on.
   Because of this, it is not necessary to do deep comparisons.
   Additionally, signatures can be compared by pointer, and a deep matching
   function is no longer necessary. */
lily_sig *lily_ensure_unique_sig(lily_symtab *symtab, lily_sig *input_sig)
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

    /* Lists that hold something circular can be something circular themselves.
       Hashes are circular if they have a value that's circular (the key is
       never circular). */
    if ((input_sig->cls->id == SYM_CLASS_LIST &&
         input_sig->siglist[0]->flags & SIG_MAYBE_CIRCULAR) ||
        (input_sig->cls->id == SYM_CLASS_HASH &&
         input_sig->siglist[1]->flags & SIG_MAYBE_CIRCULAR))
        input_sig->flags |= SIG_MAYBE_CIRCULAR;

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
