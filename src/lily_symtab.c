#include <ctype.h>
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
    * During symtab initialization, lily_raise cannot be called because the
      parser is not completely allocated and set.
**/

#define malloc_mem(size)             symtab->mem_func(NULL, size)
#define free_mem(ptr)          (void)symtab->mem_func(ptr, 0)

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

static lily_symbol_val *make_new_symbol(lily_symtab *symtab, char *data)
{
    int data_len = strlen(data);
    lily_symbol_val *ret = malloc_mem(sizeof(lily_symbol_val));
    int is_simple = 1;

    ret->string = malloc_mem(data_len + 1);
    ret->refcount = 1;
    ret->size = data_len;
    strcpy(ret->string, data);

    /* The data portion of a symbol was either a plain identifier of a valid
       string. Anything over 128 is allowed because */
    int i;
    for (i = 0;i < data_len;i++) {
        char ch = data[i];
        /* Valid utf-8/identifiers can be printed as-is. It's expected that this
           will be the most common case. */
        if (isalnum(ch) || ch > 0x7f || ch == '_')
            continue;

        is_simple = 0;
        break;
    }

    /* The special case of an empty symbol (created by a string) should be
       marked as not being simple. This will cause it to be printed as :""
       instead of just : . The former seems less confusing (and what Ruby does,
       as well). */
    if (data_len == 0)
        is_simple = 0;

    ret->is_simple = is_simple;
    return ret;
}

/*  make_new_literal
    Attempt to add a new literal of the given class to the symtab. The literal
    will be created with the given value (copying it if it's a string).

    The newly-created literal is returned. */
static lily_literal *make_new_literal(lily_symtab *symtab, lily_class *cls)
{
    lily_literal *lit = malloc_mem(sizeof(lily_literal));

    /* Literal values always have a default type, so this is safe. */
    lit->type = cls->type;

    lit->flags = SYM_TYPE_LITERAL;
    /* Literals aren't put in registers, but they ARE put in a special vm
       table. This is the literal's index into that table. */
    lit->reg_spot = symtab->next_lit_spot;
    symtab->next_lit_spot++;

    lit->next = symtab->lit_chain;
    symtab->lit_chain = lit;

    return lit;
}

/*  lily_new_var
    Attempt to create a new var in the symtab that will have the given
    type and name. The flags given are used to determine if the var is
    'readonly'. If it's readonly, it doesn't go into the vm's registers.

    On success: Returns a newly-created var that is automatically added to the
                symtab.
    On failure: NULL is returned. */
lily_var *lily_new_var(lily_symtab *symtab, lily_type *type, char *name,
        int flags)
{
    lily_var *var = malloc_mem(sizeof(lily_var));

    var->name = malloc_mem(strlen(name) + 1);
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
    lily_var *v = lily_new_var(symtab, type, name, 0);

    v->line_num = line_num;
    return v;
}

/*  get_generic_max
    Recurse into a type and determine the number of generics used. This
    is important for emitter, which needs to know how many types to blank before
    evaluating a call.

    type:        The type to check.
    generic_max: This is a pointer set to the number of generics that the
                 given type takes (generic index + 1). This is 0 if the given
                 type does not use generics. */
static void get_generic_max(lily_type *type, int *generic_max)
{
    /* function uses NULL at [1] to mean it takes no args, and NULL at [0] to
       mean that nothing is returned. */
    if (type == NULL)
        return;

    if (type->cls->id == SYM_CLASS_GENERIC) {
        if ((type->generic_pos + 1) > *generic_max)
            *generic_max = type->generic_pos + 1;
    }
    else if (type->subtypes) {
        int i;
        for (i = 0;i < type->subtype_count;i++)
            get_generic_max(type->subtypes[i], generic_max);
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
    generic class, determine how many generics the type uses.

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

        /* Find out the highest generic index that this type has inside of it.
           For functions, this allows the emitter to reserve blank types for
           holding generic matches. For other types, it allows the emitter to
           determine if a call result uses generics (since it has to be broken
           down if it does. */
        if (input_type->cls->id != SYM_CLASS_GENERIC) {
            int max = 0;
            get_generic_max(input_type, &max);
            input_type->generic_pos = max;
        }
    }

    /* This gives emitter and vm an easy way to check if a type needs to be
       resolved or if it can used as-is. */
    if (input_type->cls->id == SYM_CLASS_GENERIC ||
        input_type->generic_pos != 0) {
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
            /* Make sure scan_seed_type doesn't create non-unique generics. */
            else if (input_type->cls->id == SYM_CLASS_GENERIC &&
                     input_type->generic_pos == iter_type->generic_pos) {
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
        free_mem(input_type->subtypes);
        free_mem(input_type);

        input_type = iter_type;
    }

    return input_type;
}

static lily_type *lookup_generic(lily_symtab *symtab, const char *name)
{
    int id = name[0] - 'A';
    lily_type *type_iter = symtab->generic_type_start;

    while (id) {
        if (type_iter->next->cls != symtab->generic_class)
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

static lily_class *find_class(lily_class *class_iter, const char *name,
        uint64_t shorthash)
{
    while (class_iter) {
        if (class_iter->shorthash == shorthash &&
            strcmp(class_iter->name, name) == 0)
            break;

        class_iter = class_iter->next;
    }

    return class_iter;
}

static lily_var *find_var(lily_var *var_iter, char *name, uint64_t shorthash)
{
    while (var_iter != NULL) {
        if (var_iter->shorthash == shorthash &&
            ((var_iter->flags & SYM_OUT_OF_SCOPE) == 0) &&
            strcmp(var_iter->name, name) == 0) {

            break;
            
        }
        var_iter = var_iter->next;
    }

    return var_iter;
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
        if (arg_class->type && arg_class->id != SYM_CLASS_GENERIC)
            ret = arg_class->type;
        else {
            lily_type *complex_type = lily_type_for_class(symtab, arg_class);
            lily_type **subtypes;
            int subtype_count;
            int flags = 0;

            if (arg_id == SYM_CLASS_GENERIC) {
                if (complex_type)
                    complex_type->generic_pos = arg_ids[seed_pos];
                else
                    *ok = 0;

                seed_pos++;
                subtypes = NULL;
                subtype_count = 0;
            }
            else {
                if (arg_class->generic_count == -1) {
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
                    subtype_count = arg_class->generic_count;
                    flags = 0;
                }

                subtypes = malloc_mem(subtype_count * sizeof(lily_type *));
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
                        free_mem(subtypes);
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
static void call_class_setups(lily_symtab *symtab)
{
    lily_class *class_iter = symtab->class_chain;
    while (class_iter) {
        if (class_iter->setup_func)
            class_iter->setup_func(class_iter);

        class_iter = class_iter->next;
    }
}

/*  init_lily_main
    Symtab init, stage 3
    This creates __main__, which holds all code that is not explicitly put
    inside of a Lily function. */
static void init_lily_main(lily_symtab *symtab)
{
    lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_FUNCTION);
    lily_type *new_type = lily_type_for_class(symtab, cls);

    new_type->subtypes = malloc_mem(2 * sizeof(lily_type));
    new_type->subtypes[0] = NULL;
    new_type->subtype_count = 1;
    new_type->generic_pos = 0;
    new_type->flags = 0;

    symtab->main_var = lily_new_var(symtab, new_type, "__main__", 0);
}

/* init_classes
   Symtab init, stage 2
   This function initializes the classes of a symtab, as well as their
   types. All classes are given a type so that types which don't
   require extra call/internal element info (integer and number, for example),
   can be shared. All a symbol needs to do is sym->type to get the common
   type. */
static void init_classes(lily_symtab *symtab)
{
    int i, class_count;

    class_count = sizeof(class_seeds) / sizeof(class_seeds[0]);

    for (i = 0;i < class_count;i++) {
        lily_class *new_class = malloc_mem(sizeof(lily_class));

        lily_type *type;

        /* If a class doesn't take generics (or isn't the generic class), then
           it can have a default type that lily_type_for_class can yield.
           This saves memory, and is necessary now that type comparison is
           by pointer. */
        if (class_seeds[i].generic_count != 0)
            type = NULL;
        else {
            /* A basic class? Make a quick default type for it. */
            type = malloc_mem(sizeof(lily_type));
            type->cls = new_class;
            /* Make sure this is null so any attempt to free it won't
               cause a problem. */
            type->subtypes = NULL;
            type->subtype_count = 0;
            type->flags = 0;
            /* This means that the type does not accept subtypes within []. */
            type->generic_pos = 0;
            if (i == SYM_CLASS_ANY)
                type->flags |= TYPE_MAYBE_CIRCULAR;

            type->next = symtab->root_type;
            symtab->root_type = type;
            /* Only the generic class has a blank name (to prevent it
               from being used directly). */
            if (strcmp(class_seeds[i].name, "") == 0) {
                symtab->generic_class = new_class;
                symtab->generic_type_start = type;
            }
        }
        new_class->name = class_seeds[i].name;
        new_class->call_chain = NULL;
        new_class->type = type;
        new_class->id = i;
        new_class->generic_count = class_seeds[i].generic_count;
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

    symtab->builtin_import->class_chain = symtab->class_chain;

    /* No direct declaration of packages (for now?) */
    lily_class *package_cls = lily_class_by_id(symtab, SYM_CLASS_PACKAGE);
    package_cls->shorthash = 0;

    symtab->next_class_id = i + 1;
}

/*  lily_new_symtab:
    Symtab init, stage 1
    This creates a new symtab, then calls the init stages in order.

    On success: The newly-created symtab is returned.
    On failure: NULL is returned. */
lily_symtab *lily_new_symtab(lily_mem_func mem_func,
        lily_import_entry *builtin_import, lily_raiser *raiser)
{
    lily_symtab *symtab = mem_func(NULL, sizeof(lily_symtab));
    symtab->mem_func = mem_func;
    symtab->raiser = raiser;

    uint16_t v = 0;

    symtab->next_register_spot = 0;
    symtab->next_lit_spot = 0;
    symtab->next_function_spot = 0;
    symtab->var_chain = NULL;
    symtab->main_var = NULL;
    symtab->old_function_chain = NULL;
    symtab->class_chain = NULL;
    symtab->lit_chain = NULL;
    symtab->builtin_import = builtin_import;
    symtab->active_import = builtin_import;
    symtab->function_depth = 1;
    /* lily_new_var expects lex_linenum to be the lexer's line number.
       0 is used, because these are all builtins, and the lexer may have failed
       to initialize anyway. */
    symtab->lex_linenum = &v;
    symtab->root_type = NULL;
    symtab->generic_class = NULL;
    symtab->generic_type_start = NULL;
    symtab->old_class_chain = NULL;
    symtab->foreign_symbols = NULL;

    init_classes(symtab);
    init_lily_main(symtab);
    call_class_setups(symtab);

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
void free_vars(lily_symtab *symtab, lily_var *var)
{
    lily_var *var_temp;

    while (var != NULL) {
        var_temp = var->next;
        if ((var->flags & VAL_IS_NIL) == 0) {
            int cls_id = var->type->cls->id;
            if (cls_id == SYM_CLASS_FUNCTION)
                lily_deref_function_val(symtab->mem_func, var->value.function);
            else
                lily_deref_unknown_raw_val(symtab->mem_func,
                        var->type, var->value);
        }
        free_mem(var->name);
        free_mem(var);

        var = var_temp;
    }
}

/*  free_properties
    Free property information associated with a given class. */
static void free_properties(lily_symtab *symtab, lily_class *cls)
{
    lily_prop_entry *prop_iter = cls->properties;
    lily_prop_entry *next_prop;
    while (prop_iter) {
        next_prop = prop_iter->next;

        free_mem(prop_iter->name);
        free_mem(prop_iter);

        prop_iter = next_prop;
    }
}

/*  free_lily_main
    Regular function teardown can't be done on __main__ because __main__ does
    not keep a copy of function names. So it uses this. */
static void free_lily_main(lily_symtab *symtab, lily_function_val *fv)
{
    free_mem(fv->reg_info);
    free_mem(fv->code);
    free_mem(fv);
}

static void free_class_entries(lily_symtab *symtab, lily_class *class_iter)
{
    while (class_iter) {
        if (class_iter->properties != NULL)
            free_properties(symtab, class_iter);

        if (class_iter->call_chain != NULL)
            free_vars(symtab, class_iter->call_chain);

        class_iter = class_iter->next;
    }
}

static void free_classes(lily_symtab *symtab, lily_class *class_iter)
{
    while (class_iter) {
        /* todo: Probably a better way to do this... */
        if (class_iter->id > SYM_CLASS_PACKAGE)
            free_mem(class_iter->name);

        if (class_iter->flags & CLS_ENUM_IS_SCOPED) {
            /* Scoped enums pull the classes from the symtab's class chain so
               that parser won't find them. */
            int i;
            for (i = 0;i < class_iter->variant_size;i++) {
                free_mem(class_iter->variant_members[i]->name);
                free_mem(class_iter->variant_members[i]);
            }
        }

        free_mem(class_iter->variant_members);

        lily_class *class_next = class_iter->next;
        free_mem(class_iter);
        class_iter = class_next;
    }
}

static void free_foreign_symbols(lily_symtab *symtab)
{
    lily_weak_symbol_entry *foreign_iter = symtab->foreign_symbols;
    lily_weak_symbol_entry *foreign_next;

    while (foreign_iter) {
        foreign_next = foreign_iter->next;

        lily_symbol_val *symv = foreign_iter->symbol;
        if (symv)
            lily_deref_symbol_val(symtab->mem_func, symv);

        free_mem(foreign_iter);

        foreign_iter = foreign_next;
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

        if (lit->type->cls->id == SYM_CLASS_STRING ||
            lit->type->cls->id == SYM_CLASS_BYTESTRING)
            lily_deref_string_val(symtab->mem_func, lit->value.string);
        else if (lit->type->cls->id == SYM_CLASS_SYMBOL) {
            lily_symbol_val *symv = lit->value.symbol;
            /* Important! If not fixed, deref will ignore this value. */
            symv->has_literal = 0;
            /* Since this symbol was created internally, string::to_sym should
               have marked the symbol protected so that it would not receive
               refs. Because of that, the refcount -should- be at 0 and one
               deref will suffice. */
            lily_deref_symbol_val(symtab->mem_func, symv);
        }
        free_mem(lit);

        lit = lit_temp;
    }

    /* This should be okay, because nothing will want to use the vars at this
       point. */
    lily_function_val *main_var;

    free_class_entries(symtab, symtab->class_chain);
    free_class_entries(symtab, symtab->old_class_chain);

    if (symtab->main_var &&
        ((symtab->main_var->flags & VAL_IS_NIL) == 0))
        main_var = symtab->main_var->value.function;
    else
        main_var = NULL;

    free_foreign_symbols(symtab);

    lily_import_entry *import_iter = symtab->builtin_import;
    while (import_iter) {
        free_class_entries(symtab, import_iter->class_chain);
        free_vars(symtab, import_iter->var_chain);

        import_iter = import_iter->root_next;
    }

    if (symtab->var_chain != NULL)
        free_vars(symtab, symtab->var_chain);
    if (symtab->old_function_chain != NULL)
        free_vars(symtab, symtab->old_function_chain);

    if (main_var != NULL)
        free_lily_main(symtab, main_var);
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
        free_mem(type->subtypes);
        free_mem(type);
        type = type_temp;
    }

    lily_import_entry *import_iter = symtab->builtin_import;
    while (import_iter) {
        free_classes(symtab, import_iter->class_chain);

        import_iter = import_iter->root_next;
    }

    free_classes(symtab, symtab->old_class_chain);
    free_classes(symtab, symtab->class_chain);

    free_mem(symtab);
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
        ret = make_new_literal(symtab, integer_cls);
        ret->value.integer = int_val;
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
        ret = make_new_literal(symtab, double_cls);
        ret->value.doubleval = dbl_val;
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
        char *string_buffer = malloc_mem((want_string_len + 1) * sizeof(char));
        lily_string_val *sv = malloc_mem(sizeof(lily_string_val));

        strcpy(string_buffer, want_string);
        sv->string = string_buffer;
        sv->size = want_string_len;
        sv->refcount = 1;

        ret = make_new_literal(symtab, cls);
        ret->value.string = sv;
    }

    return ret;
}

lily_literal *lily_get_bytestring_literal(lily_symtab *symtab,
        char *want_string, int want_string_len)
{
    lily_literal *lit, *ret;
    ret = NULL;

    for (lit = symtab->lit_chain;lit;lit = lit->next) {
        if (lit->type->cls->id == SYM_CLASS_BYTESTRING) {
            if (lit->value.string->size == want_string_len &&
                memcmp(lit->value.string->string, want_string,
                        want_string_len) == 0) {
                ret = lit;
                break;
            }
        }
    }

    if (ret == NULL) {
        lily_class *cls = lily_class_by_id(symtab, SYM_CLASS_BYTESTRING);
        char *buffer = malloc_mem(want_string_len * sizeof(char));
        lily_string_val *bv = malloc_mem(sizeof(lily_string_val));

        memcpy(buffer, want_string, want_string_len);
        bv->string = buffer;
        bv->size = want_string_len;
        bv->refcount = 1;

        ret = make_new_literal(symtab, cls);
        ret->value.string = bv;
    }

    return ret;
}

lily_literal *lily_get_symbol_literal(lily_symtab *symtab, char *want_string)
{
    lily_literal *lit, *ret;
    ret = NULL;
    int want_string_len = strlen(want_string);

    for (lit = symtab->lit_chain;lit;lit = lit->next) {
        if (lit->type->cls->id == SYM_CLASS_SYMBOL) {
            if (lit->value.symbol->size == want_string_len &&
                strcmp(lit->value.symbol->string, want_string) == 0) {
                ret = lit;
                break;
            }
        }
    }

    if (ret)
        return ret;

    /* There's no literal with this symbol just yet, so make a new literal to
       hold it. From here, determine if the symbol has been created by a foreign
       source from a previous run. */

    lily_class *symbol_cls = lily_class_by_id(symtab, SYM_CLASS_SYMBOL);
    lily_literal *new_lit = make_new_literal(symtab, symbol_cls);
    lily_weak_symbol_entry *foreign_iter = symtab->foreign_symbols;
    lily_symbol_val *symv = NULL;

    while (foreign_iter) {
        if (foreign_iter->symbol &&
            foreign_iter->symbol->size == want_string_len &&
            strcmp(foreign_iter->symbol->string, want_string) == 0) {
            symv = foreign_iter->symbol;
            /* This symbol is going to be tied to a literal instead of this
               weak entry, so clear up the weak entry for a future symbol value
               to come in. */
            foreign_iter->symbol = NULL;
            break;
        }
        foreign_iter = foreign_iter->next;
    }

    if (symv == NULL) {
        symv = make_new_symbol(symtab, want_string);
    }

    symv->has_literal = 1;
    symv->assoc_lit = new_lit;

    new_lit->value.symbol = symv;

    return new_lit;
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
        ret = make_new_literal(symtab, variant_type->cls);
        ret->value.integer = 0;
    }

    return ret;
}

/*  lily_type_for_class
    Attempt to get the default type of the given class. If the given class
    doesn't have a default type (because it takes subtypes), then create
    a new type without the subtypes and return that. */
lily_type *lily_type_for_class(lily_symtab *symtab, lily_class *cls)
{
    lily_type *type;

    /* init_classes doesn't make a default type for classes that need complex
       types. This works so long as init_classes works right.
       The second part is so that seed scanning doesn't tamper with the type of
       the generic class (which gets changed around so that parser can
       understand generics). */
    if (cls->type == NULL || cls->id == SYM_CLASS_GENERIC) {
        type = malloc_mem(sizeof(lily_type));
        if (type != NULL) {
            type->cls = cls;
            type->subtypes = NULL;
            type->subtype_count = 0;
            type->flags = 0;
            type->generic_pos = 0;

            type->next = symtab->root_type;
            symtab->root_type = type;
        }
    }
    else
        type = cls->type;

    return type;
}

lily_class *find_class_by_id(lily_class *class_iter, int class_id)
{
    while (class_iter) {
        if (class_iter->id == class_id)
            break;

        class_iter = class_iter->next;
    }

    return class_iter;
}

lily_class *lily_class_by_id(lily_symtab *symtab, int class_id)
{
    lily_class *result = find_class_by_id(symtab->builtin_import->class_chain,
            class_id);
    if (result == NULL)
        result = find_class_by_id(symtab->class_chain, class_id);

    return result;
}

lily_symbol_val *lily_symbol_by_name(lily_symtab *symtab, char *text)
{
    int text_len = strlen(text);
    lily_symbol_val *ret = NULL;
    lily_literal *lit;

    for (lit = symtab->lit_chain;lit;lit = lit->next) {
        if (lit->type->cls->id == SYM_CLASS_SYMBOL) {
            if (lit->value.symbol->size == text_len &&
                strcmp(lit->value.symbol->string, text) == 0) {
                ret = lit->value.symbol;
                break;
            }
        }
    }

    if (ret == NULL) {
        lily_weak_symbol_entry *weak_entry =
                malloc_mem(sizeof(lily_weak_symbol_entry));
        lily_symbol_val *symbol = make_new_symbol(symtab, text);

        symbol->has_literal = 0;
        symbol->entry = weak_entry;

        weak_entry->next = symtab->foreign_symbols;
        weak_entry->symbol = symbol;

        symtab->foreign_symbols = weak_entry;

        ret = symbol;
    }

    return ret;
}

/*  lily_class_by_name
    Try to find a class from a given non-NULL name.
    On success: The class is returned.
    On failure: NULL is returned. */
lily_class *lily_class_by_name(lily_symtab *symtab, const char *name)
{
    uint64_t shorthash = shorthash_for_name(name);
    lily_class *class_iter = find_class(symtab->builtin_import->class_chain,
            name, shorthash);
    if (class_iter)
        return class_iter;

    class_iter = find_class(symtab->class_chain, name, shorthash);
    if (class_iter)
        return class_iter;

    /* The parser wants to be able to find classes by name...but it would be
       a waste to have lots of classes that never actually get used. The parser
       -really- just wants to get the type, so... */
    if (class_iter == NULL && name[1] == '\0') {
        lily_type *generic_type = lookup_generic(symtab, name);
        if (generic_type) {
            class_iter = symtab->generic_class;
            class_iter->type = generic_type;
        }
    }

    return class_iter;
}

lily_class *lily_class_by_name_within(lily_import_entry *import, char *name)
{
    uint64_t shorthash = shorthash_for_name(name);
    return find_class(import->class_chain, name, shorthash);
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

    while (var) {
        if (var->shorthash == shorthash &&
            ((var->flags & SYM_OUT_OF_SCOPE) == 0) &&
            strcmp(var->name, name) == 0) {
            break;
        }
        var = var->next;
    }

    return var;
}

lily_var *lily_var_by_name(lily_symtab *symtab, char *name)
{
    uint64_t shorthash = shorthash_for_name(name);
    lily_var *result = find_var(symtab->builtin_import->var_chain, name,
            shorthash);
    if (result)
        return result;

    return find_var(symtab->var_chain, name, shorthash);
}

lily_var *lily_var_by_name_within(lily_import_entry *import, char *name)
{
    uint64_t shorthash = shorthash_for_name(name);

    return find_var(import->var_chain, name, shorthash);
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

/*  lily_type_from_ids
    This is used by the apache module to create a type in a less-awful
    way than doing it manually. This unfortunately uses the really awful seed
    scanning functions.
    In the future, there will be something to get a type from a string. */
lily_type *lily_type_from_ids(lily_symtab *symtab, const int *ids)
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

    A unique, valid type is always returned. */
lily_type *lily_build_ensure_type(lily_symtab *symtab, lily_class *cls,
        int flags, lily_type **subtypes, int offset, int entries_to_use)
{
    lily_type fake_type;

    fake_type.cls = cls;
    fake_type.generic_pos = 0;
    fake_type.subtypes = subtypes + offset;
    fake_type.subtype_count = entries_to_use;
    fake_type.flags = flags;
    fake_type.next = NULL;

    /* The reason it's done like this is purely to save memory. There's no
       point in creating a new type if it already exists (since that just
       means the new one has to be destroyed). */
    lily_type *result_type = lookup_type(symtab, &fake_type);
    if (result_type == NULL) {
        lily_type *new_type = malloc_mem(sizeof(lily_type));
        lily_type **new_subtypes = malloc_mem(entries_to_use *
                sizeof(lily_type *));

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
lily_prop_entry *lily_add_class_property(lily_symtab *symtab, lily_class *cls,
        lily_type *type, char *name, int flags)
{
    lily_prop_entry *entry = malloc_mem(sizeof(lily_prop_entry));
    char *entry_name = malloc_mem(strlen(name) + 1);

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
    The newly-created class is returned. */
lily_class *lily_new_class(lily_symtab *symtab, char *name)
{
    lily_class *new_class = malloc_mem(sizeof(lily_class));
    char *name_copy = malloc_mem(strlen(name) + 1);

    strcpy(name_copy, name);

    new_class->flags = 0;
    new_class->is_refcounted = 1;
    new_class->type = NULL;
    new_class->parent = NULL;
    new_class->shorthash = shorthash_for_name(name);
    new_class->name = name_copy;
    new_class->generic_count = 0;
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
        if (cls->generic_count == 0) {
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
    /* The symtab special cases all types holding generic information so
       that they're unique, together, and in numerical order. */
    lily_type *type_iter = symtab->generic_type_start;
    int i = 1, save_count = count;

    while (count) {
        type_iter->flags &= ~TYPE_HIDDEN_GENERIC;
        count--;
        if (type_iter->next->cls != symtab->generic_class && count) {
            lily_type *new_type = malloc_mem(sizeof(lily_type));

            new_type->cls = symtab->generic_class;
            new_type->subtypes = NULL;
            new_type->subtype_count = 0;
            new_type->flags = TYPE_IS_UNRESOLVED;
            new_type->generic_pos = i;

            new_type->next = type_iter->next;
            type_iter->next = new_type;
        }
        i++;
        type_iter = type_iter->next;
    }

    if (type_iter->cls == symtab->generic_class) {
        while (type_iter->cls == symtab->generic_class) {
            type_iter->flags |= TYPE_HIDDEN_GENERIC;
            type_iter = type_iter->next;
        }
    }

    if (decl_class)
        decl_class->generic_count = save_count;
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
    lily_type *type = malloc_mem(sizeof(lily_type));

    lily_class *target_class = symtab->class_chain;
    if (target_class->generic_count != 0) {
        int count = target_class->generic_count;

        type->subtypes = malloc_mem(count * sizeof(lily_type *));

        lily_type *type_iter = symtab->generic_type_start;
        int i;
        for (i = 0;i < count;i++, type_iter = type_iter->next)
            type->subtypes[i] = type_iter;

        type->flags = TYPE_IS_UNRESOLVED;
        type->subtype_count = count;
        type->generic_pos = i;
    }
    else {
        /* This makes this type the default for this class, because this class
           doesn't use generics. */
        target_class->type = type;
        type->subtypes = NULL;
        type->subtype_count = 0;
        type->generic_pos = 0;
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

    The newly-made variant class is returned. */
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

    Note: A variant's generic_count is set within parser, when the return of a
          variant is calculated (assuming it takes arguments). */
void lily_finish_variant_class(lily_symtab *symtab, lily_class *variant_class,
        lily_type *variant_type)
{
    if (variant_type == NULL) {
        /* This variant doesn't take parameters, so give it a plain type. */
        lily_type *type = lily_type_for_class(symtab, variant_class);

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

    lily_class **members = malloc_mem(variant_count * sizeof(lily_class *));

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

void lily_enter_import(lily_symtab *symtab, lily_import_entry *entry)
{
    entry->prev_entered = symtab->active_import;
    symtab->active_import->class_chain = symtab->class_chain;
    symtab->active_import->var_chain = symtab->var_chain;
    symtab->class_chain = entry->class_chain;
    symtab->var_chain = entry->var_chain;
    symtab->active_import = entry;
}

void lily_leave_import(lily_symtab *symtab)
{
    lily_import_link *new_link = malloc_mem(sizeof(lily_import_link));
    new_link->entry = symtab->active_import;

    symtab->active_import->var_chain = symtab->var_chain;
    symtab->active_import->class_chain = symtab->class_chain;
    symtab->active_import = symtab->active_import->prev_entered;

    symtab->var_chain = symtab->active_import->var_chain;
    symtab->class_chain = symtab->active_import->class_chain;

    /* Without this, there is no (easy) way to touch each var + class in the
       symtab exactly once. That's because the active import may point to some
       non-current var/class within symtab's direct class/var lists.
       It's really important that the above IS possible, because it would be
       really bad to deref something twice. */
    symtab->active_import->var_chain = NULL;
    symtab->active_import->class_chain = NULL;

    new_link->next_import = symtab->active_import->import_chain;
    symtab->active_import->import_chain = new_link;
}

lily_import_entry *lily_find_import_within(lily_import_entry *import,
        char *name)
{
    lily_import_link *link_iter = import->import_chain;
    lily_import_entry *result = NULL;
    while (link_iter) {
        if (link_iter->entry->loadname &&
            strcmp(link_iter->entry->loadname, name) == 0) {
            result = link_iter->entry;
            break;
        }

        link_iter = link_iter->next_import;
    }

    return result;
}
