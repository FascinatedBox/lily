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
    /* Literal values always have a default type, so this is safe. */
    lit->type = cls->type;

    lit->flags = SYM_TYPE_LITERAL;
    lit->value = value;
    /* Literals aren't put in registers, but they ARE put in a special vm
       table. This is the literal's index into that table. */
    lit->reg_spot = symtab->next_lit_spot;
    symtab->next_lit_spot++;

    lit->next = symtab->lit_chain;
    symtab->lit_chain = lit;

    return lit;
}

/*  lily_try_new_var
    Attempt to create a new var in the symtab that will have the given
    type and name. The flags given are used to determine if the var is
    'readonly'. If it's readonly, it doesn't go into the vm's registers.

    On success: Returns a newly-created var that is automatically added to the
                symtab.
    On failure: NULL is returned. */
lily_var *lily_try_new_var(lily_symtab *symtab, lily_type *type, char *name,
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
    var->type = type;
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

    var->next = symtab->var_chain;
    symtab->var_chain = var;

    return var;
}

lily_var *lily_declare_var(lily_symtab *symtab, lily_type *type,
        char *name, uint16_t line_num)
{
    lily_var *v = lily_try_new_var(symtab, type, name, 0);
    if (v == NULL)
        lily_raise_nomem(symtab->raiser);

    v->line_num = line_num;
    return v;
}

/*  get_template_max
    Recurse into a type and determine the number of templates used. This
    is important for emitter, which needs to know how many types to blank before
    evaluating a call.

    type:          The type to check.
    template_max: This is a pointer set to the number of templates that the
                  given type takes (template index + 1). This is 0 if the given
                  type does not use templates. */
static void get_template_max(lily_type *type, int *template_max)
{
    /* function uses NULL at [1] to mean it takes no args, and NULL at [0] to
       mean that nothing is returned. */
    if (type == NULL)
        return;

    if (type->cls->id == SYM_CLASS_TEMPLATE) {
        if ((type->template_pos + 1) > *template_max)
            *template_max = type->template_pos + 1;
    }
    else if (type->subtypes) {
        int i;
        for (i = 0;i < type->subtype_count;i++)
            get_template_max(type->subtypes[i], template_max);
    }
}

#define SKIP_FLAGS \
    ~(TYPE_MAYBE_CIRCULAR | TYPE_CALL_HAS_ENUM_ARG | TYPE_IS_UNRESOLVED)

/*  lookup_type
    Determine if the current type exists in the symtab.

    Success: The type from the symtab is returned.
    Failure: NULL is returned. */
static lily_type *lookup_type(lily_symtab *symtab, lily_type *input_type)
{
    lily_type *iter_type = symtab->root_type;
    lily_type *ret = NULL;

    /* This just means that input_type was the last type created. */
    if (iter_type == input_type)
        iter_type = iter_type->next;

    while (iter_type) {
        if (iter_type->cls == input_type->cls) {
            if (iter_type->subtypes      != NULL &&
                iter_type->subtype_count == input_type->subtype_count &&
                iter_type               != input_type &&
                (iter_type->flags & SKIP_FLAGS) ==
                 (input_type->flags & SKIP_FLAGS)) {
                int i, match = 1;
                for (i = 0;i < iter_type->subtype_count;i++) {
                    if (iter_type->subtypes[i] != input_type->subtypes[i]) {
                        match = 0;
                        break;
                    }
                }

                if (match == 1) {
                    ret = iter_type;
                    break;
                }
            }
        }

        iter_type = iter_type->next;
    }

    return ret;
}

#undef SKIP_FLAGS

/*  finalize_type
    Determine if the given type is circular. Also, if its class is not the
    template class, determine how many templates the type uses.

    For function types, this also checks if any arguments are an enum
    class. If they are, then the type is marked to help out emitter's call
    argument processing.

    The symtab doesn't use this information at all. These are convenience
    things for the emitter and the vm. */
static void finalize_type(lily_type *input_type)
{
    if (input_type->subtypes) {
        /* functions are not containers, so circularity doesn't apply to them. */
        if (input_type->cls->id != SYM_CLASS_FUNCTION) {
            int i;
            for (i = 0;i < input_type->subtype_count;i++) {
                if (input_type->subtypes[i]->flags & TYPE_MAYBE_CIRCULAR) {
                    input_type->flags |= TYPE_MAYBE_CIRCULAR;
                    break;
                }
            }
        }

        /* Find out the highest template index that this type has inside of it.
           For functions, this allows the emitter to reserve blank types for
           holding template matches. For other types, it allows the emitter to
           determine if a call result uses templates (since it has to be broken
           down if it does. */
        if (input_type->cls->id != SYM_CLASS_TEMPLATE) {
            int max = 0;
            get_template_max(input_type, &max);
            input_type->template_pos = max;
        }
    }

    /* This gives emitter and vm an easy way to check if a type needs to be
       resolved or if it can used as-is. */
    if (input_type->cls->id == SYM_CLASS_TEMPLATE ||
        input_type->template_pos != 0) {
        input_type->flags |= TYPE_IS_UNRESOLVED;
    }

    /* It helps the emitter to know if a call has an argument that is an enum
       class, since it has to do a second reboxing pass in that case. Mark
       function types here, because all function types will have to pass through
       here. */
    if (input_type->cls->id == SYM_CLASS_FUNCTION) {
        int i;
        /* Start at 1 because [0] is the return, and doesn't matter. */
        for (i = 1;i < input_type->subtype_count;i++) {
            if (input_type->subtypes[i]->cls->flags & CLS_ENUM_CLASS) {
                input_type->flags |= TYPE_CALL_HAS_ENUM_ARG;
                break;
            }
        }

        /* Oh, and check if the vararg part has a list of some variant type. */
        if (input_type->flags & TYPE_IS_VARARGS) {
            lily_type *vararg_list = input_type->subtypes[i - 1];
            if (vararg_list->subtypes[0]->cls->flags & CLS_ENUM_CLASS)
                input_type->flags |= TYPE_CALL_HAS_ENUM_ARG;
        }
    }

    /* fixme: Properly go over enum classes to determine circularity. */
    if (input_type->cls->flags & CLS_ENUM_CLASS)
        input_type->flags |= TYPE_MAYBE_CIRCULAR;
}

/*  ensure_unique_type
    This function is used by seed scanning to make sure that something with the
    same meaning as the given type doesn't exist. This is a good thing,
    because it allows type == type comparisons (emitter and vm do this often).

    However, this function relies upon building a type to check it. The
    problem with this is that it...trashes types if they're duplicates. So
    it's rather wasteful. This will go away when seed scanning goes away. */
static lily_type *ensure_unique_type(lily_symtab *symtab, lily_type *input_type)
{
    lily_type *iter_type = symtab->root_type;
    lily_type *previous_type = NULL;
    int match = 0;

    /* This just means that input_type was the last type created. */
    if (iter_type == input_type)
        iter_type = iter_type->next;

    while (iter_type) {
        if (iter_type->cls == input_type->cls) {
            if (iter_type->subtypes      != NULL &&
                iter_type->subtype_count == input_type->subtype_count &&
                iter_type               != input_type) {
                int i;
                match = 1;
                for (i = 0;i < iter_type->subtype_count;i++) {
                    if (iter_type->subtypes[i] != input_type->subtypes[i]) {
                        match = 0;
                        break;
                    }
                }

                if (match == 1)
                    break;
            }
            /* Make sure scan_seed_type doesn't create non-unique templates. */
            else if (input_type->cls->id == SYM_CLASS_TEMPLATE &&
                     input_type->template_pos == iter_type->template_pos) {
                match = 1;
                break;
            }
        }

        if (iter_type->next == input_type)
            previous_type = iter_type;

        iter_type = iter_type->next;
    }

    finalize_type(input_type);

    if (match) {
        /* Remove input_type from the symtab's type chain. */
        if (symtab->root_type == input_type)
            /* It is the root, so just advance the root. */
            symtab->root_type = symtab->root_type->next;
        else {
            /* Make the type before it link to the node after it. This is
               theoretically safe because the chain goes from recent to least
               recent. So this should find the input type before it finds
               one that equals it (and set previous_type to something valid). */
            previous_type->next = input_type->next;
        }

        /* This is either NULL or something that only this type uses. Don't free
           what's inside of the subtypes though, since that's other types
           still in the chain. */
        lily_free(input_type->subtypes);
        lily_free(input_type);

        input_type = iter_type;
    }

    return input_type;
}

static lily_type *lookup_generic(lily_symtab *symtab, const char *name)
{
    int id = name[0] - 'A';
    lily_type *type_iter = symtab->template_type_start;

    while (id) {
        if (type_iter->next->cls != symtab->template_class)
            break;

        type_iter = type_iter->next;
        if (type_iter->flags & TYPE_HIDDEN_GENERIC)
            break;

        id--;
    }

    if (type_iter->flags & TYPE_HIDDEN_GENERIC || id)
        type_iter = NULL;

    return type_iter;
}

/*****************************************************************************/
/* 'Seed'-handling functions                                                 */
/*****************************************************************************/

/*  scan_seed_arg
    This takes a series of int's and uses them to define a new type. This
    is currently only used by lily_cls_* files and builtin functions for
    defining function information. This also gets used to help create the type
    for properties.

    This function will be destroyed soon. Passing a series of integers makes it
    impossible to have various modules imported by the interpreter (class 50
    could be a regexp or a database class). */
static lily_type *scan_seed_arg(lily_symtab *symtab, const int *arg_ids,
        int *pos, int *ok)
{
    lily_type *ret;
    int arg_id = arg_ids[*pos];
    int seed_pos = *pos + 1;
    *ok = 1;

    if (arg_id == -1)
        ret = NULL;
    else {
        lily_class *arg_class = lily_class_by_id(symtab, arg_id);
        if (arg_class->type && arg_class->id != SYM_CLASS_TEMPLATE)
            ret = arg_class->type;
        else {
            lily_type *complex_type = lily_try_type_for_class(symtab, arg_class);
            lily_type **subtypes;
            int subtype_count;
            int flags = 0;

            if (arg_id == SYM_CLASS_TEMPLATE) {
                if (complex_type)
                    complex_type->template_pos = arg_ids[seed_pos];
                else
                    *ok = 0;

                seed_pos++;
                subtypes = NULL;
                subtype_count = 0;
            }
            else {
                if (arg_class->template_count == -1) {
                    /* -1 means it takes a specified number of values. */
                    subtype_count = arg_ids[seed_pos];
                    seed_pos++;
                    /* Function needs flags in case the thing is varargs. */
                    if (arg_id == SYM_CLASS_FUNCTION) {
                        flags = arg_ids[seed_pos];
                        seed_pos++;
                    }
                }
                else {
                    subtype_count = arg_class->template_count;
                    flags = 0;
                }

                subtypes = lily_malloc(subtype_count * sizeof(lily_type *));
                if (subtypes) {
                    int i;
                    for (i = 0;i < subtype_count;i++) {
                        subtypes[i] = scan_seed_arg(symtab, arg_ids, &seed_pos,
                                ok);

                        if (*ok == 0)
                            break;
                    }

                    if (*ok == 0) {
                        /* This isn't tied to anything, so free it. Inner args
                           have already been ensured, so don't touch them. */
                        lily_free(subtypes);
                        subtypes = NULL;
                        *ok = 0;
                    }
                }
                else
                    *ok = 0;
            }

            if (*ok == 1 && complex_type != NULL) {
                complex_type->subtypes = subtypes;
                complex_type->subtype_count = subtype_count;
                complex_type->flags = flags;
                complex_type = ensure_unique_type(symtab, complex_type);
                ret = complex_type;
            }
            else
                ret = NULL;
        }
    }

    *pos = seed_pos;
    return ret;
}

/*****************************************************************************/
/* Symtab initialization */
/*****************************************************************************/

/*  call_setups
    Symtab init, stage 4
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

/*  init_lily_main
    Symtab init, stage 3
    This creates __main__, which holds all code that is not explicitly put
    inside of a Lily function. */
static int init_lily_main(lily_symtab *symtab)
{
    lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_FUNCTION);
    lily_type *new_type = lily_try_type_for_class(symtab, cls);
    if (new_type == NULL)
        return 0;

    new_type->subtypes = lily_malloc(2 * sizeof(lily_type));
    if (new_type->subtypes == NULL)
        return 0;

    new_type->subtypes[0] = NULL;
    new_type->subtype_count = 1;
    new_type->template_pos = 0;
    new_type->flags = 0;

    symtab->main_var = lily_try_new_var(symtab, new_type, "__main__", 0);

    return (symtab->main_var != NULL);
}

/* init_classes
   Symtab init, stage 2
   This function initializes the classes of a symtab, as well as their
   types. All classes are given a type so that types which don't
   require extra call/internal element info (integer and number, for example),
   can be shared. All a symbol needs to do is sym->type to get the common
   type. */
static int init_classes(lily_symtab *symtab)
{
    int i, class_count, ret;

    class_count = sizeof(class_seeds) / sizeof(class_seeds[0]);
    ret = 1;

    for (i = 0;i < class_count;i++) {
        lily_class *new_class = lily_malloc(sizeof(lily_class));

        if (new_class != NULL) {
            lily_type *type;

            /* If a class doesn't take templates (or isn't template), then
               it can have a default type that lily_try_type_for_class can yield.
               This saves memory, and is necessary now that type comparison is
               by pointer. */
            if (class_seeds[i].template_count != 0)
                type = NULL;
            else {
                /* A basic class? Make a quick default type for it. */
                type = lily_malloc(sizeof(lily_type));
                if (type != NULL) {
                    type->cls = new_class;
                    /* Make sure this is null so any attempt to free it won't
                       cause a problem. */
                    type->subtypes = NULL;
                    type->subtype_count = 0;
                    type->flags = 0;
                    /* Non-template types use this to mean that this type
                       does not have templates inside. */
                    type->template_pos = 0;
                    if (i == SYM_CLASS_ANY)
                        type->flags |= TYPE_MAYBE_CIRCULAR;

                    type->next = symtab->root_type;
                    symtab->root_type = type;
                    /* Only the template class has a blank name (to prevent it
                       from being used directly). */
                    if (strcmp(class_seeds[i].name, "") == 0) {
                        symtab->template_class = new_class;
                        symtab->template_type_start = type;
                    }
                }
                else
                    ret = 0;
            }

            new_class->name = class_seeds[i].name;
            new_class->call_chain = NULL;
            new_class->type = type;
            new_class->id = i;
            new_class->template_count = class_seeds[i].template_count;
            new_class->shorthash = shorthash_for_name(new_class->name);
            new_class->gc_marker = class_seeds[i].gc_marker;
            new_class->flags = class_seeds[i].flags;
            new_class->is_refcounted = class_seeds[i].is_refcounted;
            new_class->seed_table = NULL;
            new_class->setup_func = class_seeds[i].setup_func;
            new_class->eq_func = class_seeds[i].eq_func;
            new_class->variant_members = NULL;
            new_class->properties = NULL;
            new_class->prop_count = 0;
            new_class->parent = NULL;

            new_class->next = symtab->class_chain;
            symtab->class_chain = new_class;
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
    }

    symtab->next_class_id = i + 1;
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
    symtab->var_chain = NULL;
    symtab->main_var = NULL;
    symtab->old_function_chain = NULL;
    symtab->class_chain = NULL;
    symtab->lit_chain = NULL;
    symtab->function_depth = 1;
    /* lily_try_new_var expects lex_linenum to be the lexer's line number.
       0 is used, because these are all builtins, and the lexer may have failed
       to initialize anyway. */
    symtab->lex_linenum = &v;
    symtab->root_type = NULL;
    symtab->template_class = NULL;
    symtab->template_type_start = NULL;
    symtab->old_class_chain = NULL;

    if (!init_classes(symtab) || !init_lily_main(symtab) ||
        !call_class_setups(symtab)) {
        /* First vars created, if any... */
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
            int cls_id = var->type->cls->id;
            if (cls_id == SYM_CLASS_FUNCTION)
                lily_deref_function_val(var->value.function);
            else
                lily_deref_unknown_raw_val(var->type, var->value);
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

static void free_class_entries(lily_class *class_iter)
{
    while (class_iter) {
        if (class_iter->properties != NULL)
            free_properties(class_iter);

        if (class_iter->call_chain != NULL)
            free_vars(class_iter->call_chain);

        class_iter = class_iter->next;
    }
}

static void free_classes(lily_class *class_iter)
{
    while (class_iter) {
        /* todo: Probably a better way to do this... */
        if (class_iter->id > SYM_CLASS_PACKAGE)
            lily_free(class_iter->name);

        if (class_iter->flags & CLS_ENUM_IS_SCOPED) {
            /* Scoped enums pull the classes from the symtab's class chain so
               that parser won't find them. */
            int i;
            for (i = 0;i < class_iter->variant_size;i++) {
                lily_free(class_iter->variant_members[i]->name);
                lily_free(class_iter->variant_members[i]);
            }
        }

        lily_free(class_iter->variant_members);

        lily_class *class_next = class_iter->next;
        lily_free(class_iter);
        class_iter = class_next;
    }
}

/*  lily_free_symtab_lits_and_vars

    This frees all literals and vars within the symtab. This is the first step
    to tearing down the symtab, with the second being to call lily_free_symtab.

    Symtab's teardown is in two steps so that the gc can have one final pass
    after the vars get a deref. This allows the gc to attempt cleanly destroying
    all values. It needs type and class info, which is why that IS NOT
    touched here.

    Additionally, parts of symtab init may have failed, so NULL checks are
    important.

    symtab: The symtab to delete the vars and literals of. */
void lily_free_symtab_lits_and_vars(lily_symtab *symtab)
{
    lily_literal *lit, *lit_temp;

    lit = symtab->lit_chain;

    while (lit != NULL) {
        lit_temp = lit->next;

        if (lit->type->cls->id == SYM_CLASS_STRING)
            lily_deref_string_val(lit->value.string);

        lily_free(lit);

        lit = lit_temp;
    }

    /* This should be okay, because nothing will want to use the vars at this
       point. */
    lily_function_val *main_vartion;

    free_class_entries(symtab->class_chain);
    free_class_entries(symtab->old_class_chain);

    if (symtab->main_var &&
        ((symtab->main_var->flags & VAL_IS_NIL) == 0))
        main_vartion = symtab->main_var->value.function;
    else
        main_vartion = NULL;

    if (symtab->var_chain != NULL)
        free_vars(symtab->var_chain);
    if (symtab->old_function_chain != NULL)
        free_vars(symtab->old_function_chain);

    if (main_vartion != NULL)
        free_lily_main(main_vartion);
}

/*  lily_free_symtab

    This destroys the classes and types stored in the symtab, as well as
    the symtab itself. This is called after the vm has had a chance to tell the
    gc to do a final sweep (where type info is necessary).

    symtab: The symtab to destroy the vars of. */
void lily_free_symtab(lily_symtab *symtab)
{
    lily_type *type, *type_temp;

    /* Destroy the types before the classes, since the types need to check
       the class id to make sure there isn't a call type to destroy. */
    type = symtab->root_type;
    int j = 0;
    while (type != NULL) {
        j++;
        type_temp = type->next;

        /* The subtypes is either NULL or set to something that needs to be
           deleted. */
        lily_free(type->subtypes);
        lily_free(type);
        type = type_temp;
    }

    free_classes(symtab->old_class_chain);
    free_classes(symtab->class_chain);

    lily_free(symtab);
}

/*****************************************************************************/
/* Exported functions                                                        */
/*****************************************************************************/

/* These next three are used to get an integer, double, or string literal.
   They first look to see of the symtab has a literal with that value, then
   attempt to create it if there isn't one. */

lily_literal *lily_get_integer_literal(lily_symtab *symtab, int64_t int_val)
{
    lily_literal *lit, *ret;
    ret = NULL;
    lily_class *integer_cls = lily_class_by_id(symtab, SYM_CLASS_INTEGER);
    lily_type *want_type = integer_cls->type;

    for (lit = symtab->lit_chain;lit != NULL;lit = lit->next) {
        if (lit->type == want_type && lit->value.integer == int_val) {
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
    lily_type *want_type = double_cls->type;

    for (lit = symtab->lit_chain;lit != NULL;lit = lit->next) {
        if (lit->type == want_type && lit->value.doubleval == dbl_val) {
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

    for (lit = symtab->lit_chain;lit;lit = lit->next) {
        if (lit->type->cls->id == SYM_CLASS_STRING) {
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

/*  lily_get_variant_literal
    This function is like the other literal getters, except that it's called
    for empty variant classes. An empty variant class will always be the same,
    so an empty literal is passed around for the value.
    Otherwise the interpreter would have to create a bunch of nothings with the
    same value, and that would be rather silly. :) */
lily_literal *lily_get_variant_literal(lily_symtab *symtab,
        lily_type *variant_type)
{
    lily_literal *lit_iter, *ret;
    ret = NULL;

    for (lit_iter = symtab->lit_chain;
         lit_iter != NULL;
         lit_iter = lit_iter->next) {
        if (lit_iter->type == variant_type) {
            ret = lit_iter;
            break;
        }
    }

    if (ret == NULL) {
        lily_raw_value v;
        v.integer = 0;
        ret = try_new_literal(symtab, variant_type->cls, v);
        if (ret == NULL)
            lily_raise_nomem(symtab->raiser);
    }

    return ret;
}

/*  lily_try_type_for_class
    Attempt to get the default type of the given class. If the given class
    doesn't have a default type (because it takes templates), then create
    a new type without a subtypes and return that. */
lily_type *lily_try_type_for_class(lily_symtab *symtab, lily_class *cls)
{
    lily_type *type;

    /* init_classes doesn't make a default type for classes that need complex
       types. This works so long as init_classes works right.
       The second part is so that seed scanning doesn't tamper with the type of
       the template class (which gets changed around so that parser can
       understand generics). */
    if (cls->type == NULL || cls->id == SYM_CLASS_TEMPLATE) {
        type = lily_malloc(sizeof(lily_type));
        if (type != NULL) {
            type->cls = cls;
            type->subtypes = NULL;
            type->subtype_count = 0;
            type->flags = 0;
            type->template_pos = 0;

            type->next = symtab->root_type;
            symtab->root_type = type;
        }
    }
    else
        type = cls->type;

    return type;
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

    /* The parser wants to be able to find classes by name...but it would be
       a waste to have lots of classes that never actually get used. The parser
       -really- just wants to get the type, so... */
    if (class_iter == NULL && name[1] == '\0') {
        lily_type *generic_type = lookup_generic(symtab, name);
        if (generic_type) {
            class_iter = symtab->template_class;
            class_iter->type = generic_type;
        }
    }

    return class_iter;
}

/*  lily_find_class_callable
    Check if a class has a given function within it. If it doesn't, see if the
    class comes with a 'seed_table' that defines more functions. If it has a
    seed table, attempt to do a dynamic load of the given function.

    This is a bit complicated, but it saves a LOT of memory from not having to
    make type+var information for every builtin thing. */
lily_var *lily_find_class_callable(lily_symtab *symtab, lily_class *cls,
        char *name)
{
    lily_var *iter;
    uint64_t shorthash = shorthash_for_name(name);

    for (iter = cls->call_chain;iter != NULL;iter = iter->next) {
        if (iter->shorthash == shorthash && strcmp(iter->name, name) == 0)
            break;
    }

    return iter;
}

/*  lily_add_class_method
    Add the given var to the methods of the given class. If the variable given
    is the current global var, the symtab's linked list of vars moves to the
    next var. */
void lily_add_class_method(lily_symtab *symtab, lily_class *cls,
        lily_var *method_var)
{
    /* Prevent class methods from being accessed globally, because they're now
       longer globals. */
    if (method_var == symtab->var_chain)
        symtab->var_chain = method_var->next;

    method_var->next = cls->call_chain;
    cls->call_chain = method_var;
}

const lily_func_seed *lily_find_class_call_seed(lily_symtab *symtab,
        lily_class *cls, char *name)
{
    const lily_func_seed *seed_iter = NULL;
    if (cls->seed_table) {
        seed_iter = cls->seed_table;
        while (seed_iter != NULL) {
            if (strcmp(seed_iter->name, name) == 0)
                break;

            seed_iter = seed_iter->next;
        }
    }

    return seed_iter;
}

const lily_func_seed *lily_get_global_seed_chain()
{
    return &GLOBAL_SEED_START;
}

/*  lily_find_property
    Attempt to find a property with the given name in the class. If the class
    given inherits other classes, then they're checked too.

    On success: A valid property entry is returned.
    On failure: NULL is returned. */
lily_prop_entry *lily_find_property(lily_symtab *symtab, lily_class *cls, char *name)
{
    lily_prop_entry *ret = NULL;

    if (cls->properties != NULL) {
        uint64_t shorthash = shorthash_for_name(name);
        lily_prop_entry *prop_iter = cls->properties;
        while (prop_iter) {
            if (prop_iter->name_shorthash == shorthash &&
                strcmp(prop_iter->name, name) == 0) {
                ret = prop_iter;
                break;
            }

            prop_iter = prop_iter->next;
        }
    }

    if (ret == NULL && cls->parent != NULL)
        ret = lily_find_property(symtab, cls->parent, name);

    return ret;
}

lily_class *lily_find_scoped_variant(lily_class *enum_class, char *name)
{
    int i;
    uint64_t shorthash = shorthash_for_name(name);
    lily_class *ret = NULL;

    for (i = 0;i < enum_class->variant_size;i++) {
        lily_class *variant_class = enum_class->variant_members[i];
        if (variant_class->shorthash == shorthash &&
            strcmp(variant_class->name, name) == 0) {
            ret = variant_class;
        }
    }

    return ret;
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
    lily_var *var = symtab->var_chain;
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
    until 'var_stop' are now out of scope. But...don't delete them because the
    emitter will need to know their type info later. */
void lily_hide_block_vars(lily_symtab *symtab, lily_var *var_stop)
{
    lily_var *var_iter = symtab->var_chain;

    while (var_iter != var_stop) {
        var_iter->flags |= SYM_OUT_OF_SCOPE;
        var_iter = var_iter->next;
    }
}

/*  lily_try_type_from_ids
    This is used by the apache module to create a type in a less-awful
    way than doing it manually. This unfortunately uses the really awful seed
    scanning functions.
    In the future, there will be something to get a type from a string. */
lily_type *lily_try_type_from_ids(lily_symtab *symtab, const int *ids)
{
    int pos = 0, ok = 1;
    return scan_seed_arg(symtab, ids, &pos, &ok);
}

/*  lily_build_ensure_type
    This function is used to ensure that creating a type for 'cls' with
    the given information will not result in a duplicate type entry.
    Unique types are a good thing, because that allows type == type
    comparisons by emitter and the vm.
    This creates a new type if, and only if, it would be unique.

    cls:            The base class to look for.
    flags:          Flags for the type. Important for functions, which
                    may/may not be TYPE_IS_VARARGS.
    subtypes:        The subtypes that proper types will be pulled from.
    offset:         In subtypes, where to start taking types.
    entries_to_use: How many types to take after 'offset'.

    This is used by parser and emitter to make sure they don't create
    types they'll have to throw away.

    This raises NoMemoryError if it needs to make a type and can't. Otherwise,
    a unique, valid type is always returned. */
lily_type *lily_build_ensure_type(lily_symtab *symtab, lily_class *cls,
        int flags, lily_type **subtypes, int offset, int entries_to_use)
{
    lily_type fake_type;

    fake_type.cls = cls;
    fake_type.template_pos = 0;
    fake_type.subtypes = subtypes + offset;
    fake_type.subtype_count = entries_to_use;
    fake_type.flags = flags;
    fake_type.next = NULL;

    /* The reason it's done like this is purely to save memory. There's no
       point in creating a new type if it already exists (since that just
       means the new one has to be destroyed). */
    lily_type *result_type = lookup_type(symtab, &fake_type);
    if (result_type == NULL) {
        lily_type *new_type = lily_malloc(sizeof(lily_type));
        lily_type **new_subtypes = lily_malloc(entries_to_use *
                sizeof(lily_type *));

        if (new_type == NULL || new_subtypes == NULL) {
            lily_free(new_type);
            lily_free(new_subtypes);
            lily_raise_nomem(symtab->raiser);
        }

        memcpy(new_type, &fake_type, sizeof(lily_type));
        memcpy(new_subtypes, subtypes + offset, sizeof(lily_type *) * entries_to_use);
        new_type->subtypes = new_subtypes;
        new_type->subtype_count = entries_to_use;

        new_type->next = symtab->root_type;
        symtab->root_type = new_type;

        finalize_type(new_type);
        result_type = new_type;
    }

    return result_type;
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

/*  lily_add_class_property
    Add a new property to the property chain of a class.
    On success: Returns the property, in case that's useful.
    On failure: NULL is returned. */
lily_prop_entry *lily_add_class_property(lily_class *cls, lily_type *type,
        char *name, int flags)
{
    lily_prop_entry *entry = lily_malloc(sizeof(lily_prop_entry));
    char *entry_name = lily_malloc(strlen(name) + 1);
    if (entry == NULL || entry_name == NULL) {
        lily_free(entry);
        lily_free(entry_name);
        return NULL;
    }

    strcpy(entry_name, name);

    entry->flags = flags;
    entry->name = entry_name;
    entry->type = type;
    entry->name_shorthash = shorthash_for_name(entry_name);
    entry->next = NULL;
    entry->id = cls->prop_count;
    cls->prop_count++;

    /* It's REALLY important that properties be linked this way, because it
       allows the vm to walk from a derived class up through the superclasses
       when setting property types in instance creation.
       It goes like this:

        Animal        >  Bird          >  Falcon
       [3 => 2 => 1] => [6 => 5 => 4] => [9 => 8 => 7] */
    entry->next = cls->properties;
    cls->properties = entry;

    return entry;
}

/*  lily_new_class
    This function creates a new user class of the given name and adds it to
    the current chain of classes. This creates a default type for the
    class that is empty, and gives basic info to the class.
    Properties can be added later via lily_add_class_property.
    On success: Returns the newly-created class.
    On failure: lily_raise_nomem is called. */
lily_class *lily_new_class(lily_symtab *symtab, char *name)
{
    lily_class *new_class = lily_malloc(sizeof(lily_class));
    char *name_copy = lily_malloc(strlen(name) + 1);

    if (new_class == NULL || name_copy == NULL) {
        lily_free(new_class);
        lily_free(name_copy);
        lily_raise_nomem(symtab->raiser);
    }

    strcpy(name_copy, name);

    new_class->flags = 0;
    new_class->is_refcounted = 1;
    new_class->type = NULL;
    new_class->parent = NULL;
    new_class->shorthash = shorthash_for_name(name);
    new_class->name = name_copy;
    new_class->template_count = 0;
    new_class->properties = NULL;
    new_class->prop_count = 0;
    new_class->seed_table = NULL;
    new_class->call_chain = NULL;
    new_class->setup_func = NULL;
    new_class->variant_members = NULL;
    new_class->gc_marker = NULL;
    new_class->eq_func = lily_instance_eq;

    new_class->id = symtab->next_class_id;
    symtab->next_class_id++;

    new_class->next = symtab->class_chain;
    symtab->class_chain = new_class;

    return new_class;
}

/*  lily_finish_class
    The given class is done. Determine if instances of it will need to have
    gc entries made for them. */
void lily_finish_class(lily_symtab *symtab, lily_class *cls)
{
    lily_prop_entry *prop_iter = cls->properties;

    if ((cls->flags & CLS_ENUM_CLASS) == 0) {
        /* If the class has no generics, determine if it's circular and write
           that information onto the default type. */
        if (cls->template_count == 0) {
            while (prop_iter) {
                if (prop_iter->type->flags & TYPE_MAYBE_CIRCULAR) {
                    cls->type->flags |= TYPE_MAYBE_CIRCULAR;
                    break;
                }
                prop_iter = prop_iter->next;
            }

            if (cls->type->flags & TYPE_MAYBE_CIRCULAR)
                cls->gc_marker = lily_gc_tuple_marker;
        }
        else
            /* Each instance of a generic class may/may not be circular depending
               on what it's given. */
            cls->gc_marker = lily_gc_tuple_marker;
    }
    else {
        /* Enum classes have the same layout as 'any', and should thus use what
           'any' uses for things. */
        cls->gc_marker = lily_gc_any_marker;
        cls->eq_func = lily_any_eq;
    }

    if (cls != symtab->old_class_chain) {
        lily_class *class_iter = symtab->class_chain;
        lily_class *class_next;

        while (class_iter != cls) {
            class_next = class_iter->next;
            class_iter->next = symtab->old_class_chain;
            symtab->old_class_chain = class_iter;
            class_iter = class_next;
        }

        symtab->class_chain = cls;
    }
}

void lily_update_symtab_generics(lily_symtab *symtab, lily_class *decl_class,
        int count)
{
    /* The symtab special cases all types holding template information so
       that they're unique, together, and in numerical order. */
    lily_type *type_iter = symtab->template_type_start;
    int i = 1, save_count = count;

    while (count) {
        type_iter->flags &= ~TYPE_HIDDEN_GENERIC;
        count--;
        if (type_iter->next->cls != symtab->template_class && count) {
            lily_type *new_type = lily_malloc(sizeof(lily_type));
            if (new_type == NULL)
                lily_raise_nomem(symtab->raiser);

            new_type->cls = symtab->template_class;
            new_type->subtypes = NULL;
            new_type->subtype_count = 0;
            new_type->flags = TYPE_IS_UNRESOLVED;
            new_type->template_pos = i;

            new_type->next = type_iter->next;
            type_iter->next = new_type;
        }
        i++;
        type_iter = type_iter->next;
    }

    if (type_iter->cls == symtab->template_class) {
        while (type_iter->cls == symtab->template_class) {
            type_iter->flags |= TYPE_HIDDEN_GENERIC;
            type_iter = type_iter->next;
        }
    }

    if (decl_class)
        decl_class->template_count = save_count;
}

/*  lily_make_constructor_return_type
    The parser is about to collect the arguments for a new class. This is used
    to create a type that the constructor will return.
    If a class has no generics, then it returns a type of just itself
    (which also becomes the default type. For the other case, the construct will
    return a type of the proper number of generics with the generics also
    being ordered. So...
    class Point[A]() # returns Point[A]
    class Point[A, B, C]() # returns Point[A, B, C]
    ...etc. */
void lily_make_constructor_return_type(lily_symtab *symtab)
{
    lily_type *type = lily_malloc(sizeof(lily_type));
    if (type == NULL)
        lily_raise_nomem(symtab->raiser);

    lily_class *target_class = symtab->class_chain;
    if (target_class->template_count != 0) {
        int count = target_class->template_count;

        type->subtypes = lily_malloc(count * sizeof(lily_type *));
        if (type->subtypes == NULL) {
            lily_free(type);
            lily_raise_nomem(symtab->raiser);
        }

        lily_type *type_iter = symtab->template_type_start;
        int i;
        for (i = 0;i < count;i++, type_iter = type_iter->next)
            type->subtypes[i] = type_iter;

        type->flags = TYPE_IS_UNRESOLVED;
        type->subtype_count = count;
        type->template_pos = i;
    }
    else {
        /* This makes this type the default for this class, because this class
           doesn't use generics. */
        target_class->type = type;
        type->subtypes = NULL;
        type->subtype_count = 0;
        type->template_pos = 0;
        type->flags = 0;
    }

    type->cls = target_class;

    type->next = symtab->root_type;
    symtab->root_type = type;
}

/*  lily_add_variant_class
    This adds a class to the symtab, marks it as a variant class, and makes it
    a child of the given enum class.

    The variant type of the class will be set when the parser has that info and
    calls lily_finish_variant_class.

    Success: The newly-made variant class is returned.
    Failure: lily_raise_nomem is called. */
lily_class *lily_new_variant_class(lily_symtab *symtab, lily_class *enum_class,
        char *name)
{
    lily_class *cls = lily_new_class(symtab, name);

    cls->flags |= CLS_VARIANT_CLASS;
    cls->parent = enum_class;

    return cls;
}

/*  lily_finish_variant_class
    This function is called when the parser has completed gathering information
    about a given variant.

    If the variant takes arguments, then variant_type is non-NULL.
    If the variant does not take arguments, a default type is made for it.

    Note: A variant's template_count is set within parser, when the return of a
          variant is calculated (assuming it takes arguments). */
void lily_finish_variant_class(lily_symtab *symtab, lily_class *variant_class,
        lily_type *variant_type)
{
    if (variant_type == NULL) {
        /* This variant doesn't take parameters, so give it a plain type. */
        lily_type *type = lily_try_type_for_class(symtab, variant_class);
        if (type == NULL)
            lily_raise_nomem(symtab->raiser);

        type->cls = variant_class;
        /* Anything that doesn't take parameters gets a default type. */
        variant_class->type = type;

        variant_class->variant_type = type;
        /* Empty variants are represented as integers, and won't need to be
           marked through. */
        variant_class->eq_func = lily_integer_eq;
    }
    else {
        variant_class->variant_type = variant_type;
        /* The only difference between a tuple and a variant with args is that
           the variant has a variant type instead of a tuple one. */
        variant_class->gc_marker = lily_gc_tuple_marker;
        variant_class->eq_func = lily_tuple_eq;
    }
}

void lily_finish_enum_class(lily_symtab *symtab, lily_class *enum_class,
        int is_scoped, lily_type *enum_type)
{
    int i, variant_count = 0;
    lily_class *class_iter = symtab->class_chain;
    while (class_iter != enum_class) {
        variant_count++;
        class_iter = class_iter->next;
    }

    lily_class **members = lily_malloc(variant_count * sizeof(lily_class *));
    if (members == NULL)
        lily_raise_nomem(symtab->raiser);

    for (i = 0, class_iter = symtab->class_chain;
         i < variant_count;
         i++, class_iter = class_iter->next)
        members[i] = class_iter;

    enum_class->variant_type = enum_type;
    enum_class->variant_members = members;
    enum_class->variant_size = variant_count;
    enum_class->flags |= CLS_ENUM_CLASS;
    enum_class->gc_marker = lily_gc_any_marker;
    enum_class->eq_func = lily_any_eq;

    if (is_scoped) {
        enum_class->flags |= CLS_ENUM_IS_SCOPED;
        /* This removes the variants from symtab's classes, so that parser has
           to get them from the enum class. */
        symtab->class_chain = enum_class;
    }
}

/*  lily_change_parent_class
    This marks the first class as being inherited by the second class. */
void lily_change_parent_class(lily_class *super_class, lily_class *sub_class)
{
    sub_class->parent = super_class;
    /* This must be copied over because the vm uses this number to determine
       how many slots to allocate for the class.
       Subclass properties can safely start after superclass properties because
       of single inheritance. */
    sub_class->prop_count = super_class->prop_count;
}
