#include <ctype.h>
#include <string.h>

#include "lily_alloc.h"
#include "lily_symtab.h"
#include "lily_pkg_builtin.h"
#include "lily_seed.h"

#include "lily_value.h"

/** Symtab is responsible for:
    * Providing functions for finding and creating classes and vars.
    * Holding 'ties' that associate vars with values (so that a vars do not
      actually hold values.
    * Creating and finding new literals.
    * Creation of new types (and ensuring that each type created is unique so
      that == can compare them.
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

/*  make_new_literal
    Attempt to add a new literal of the given class to the symtab. The literal
    will be created with the given value (copying it if it's a string).

    The newly-created literal is returned. */
static lily_tie *make_new_literal(lily_symtab *symtab, lily_class *cls)
{
    lily_tie *lit = lily_malloc(sizeof(lily_tie));

    /* Literal values always have a default type, so this is safe. */
    lit->type = cls->type;

    lit->flags = ITEM_TYPE_TIE;
    /* Literals aren't put in registers, but they ARE put in a special vm
       table. This is the literal's index into that table. */
    lit->reg_spot = symtab->next_readonly_spot;
    symtab->next_readonly_spot++;

    lit->next = symtab->literals;
    symtab->literals = lit;

    return lit;
}

/*  This creates a new var with the given type and name. This var does not have
    a reg_spot set just yet. It's also NOT linked in the symtab, and thus must
    be injected into a var_chain somewhere. */
lily_var *lily_new_raw_unlinked_var(lily_symtab *symtab, lily_type *type,
        const char *name)
{
    lily_var *var = lily_malloc(sizeof(lily_var));

    var->name = lily_malloc(strlen(name) + 1);
    var->flags = ITEM_TYPE_VAR;
    strcpy(var->name, name);
    var->line_num = *symtab->lex_linenum;

    var->shorthash = shorthash_for_name(name);
    var->type = type;
    var->next = NULL;
    var->parent = NULL;

    return var;
}

/*  Create a new var that isn't quite setup just yet. The newly-made var is
    added to the symtab, but the reg_spot of the var is not yet set. This is
    meant to be called by a lily_emit_new_*_var function. */
lily_var *lily_new_raw_var(lily_symtab *symtab, lily_type *type,
        const char *name)
{
    lily_var *var = lily_new_raw_unlinked_var(symtab, type, name);

    var->next = symtab->active_import->var_chain;
    symtab->active_import->var_chain = var;

    return var;
}

/* This creates a new type with the class set to the class given. The
   newly-created type is added to symtab's root type. */
lily_type *lily_new_type(lily_symtab *symtab, lily_class *cls)
{
    lily_type *new_type = lily_malloc(sizeof(lily_type));
    new_type->cls = cls;
    new_type->flags = 0;
    new_type->generic_pos = 0;
    new_type->subtype_count = 0;
    new_type->subtypes = NULL;

    new_type->next = symtab->root_type;
    symtab->root_type = new_type;

    return new_type;
}

/* Create a new class with the given name and add it to the classes currently
   available. */
lily_class *lily_new_class(lily_symtab *symtab, char *name)
{
    lily_class *new_class = lily_malloc(sizeof(lily_class));
    char *name_copy = lily_malloc(strlen(name) + 1);

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
    new_class->dynaload_table = NULL;
    new_class->call_chain = NULL;
    new_class->variant_members = NULL;
    new_class->gc_marker = NULL;
    new_class->eq_func = NULL;
    new_class->destroy_func = NULL;
    new_class->import = symtab->active_import;

    new_class->id = symtab->next_class_id;
    symtab->next_class_id++;

    new_class->next = symtab->active_import->class_chain;
    symtab->active_import->class_chain = new_class;

    return new_class;
}

/* This creates a new class based off of a given seed. The name is copied over
   from the seed given.
   If the given class does not take generics, this will also set the default
   type of the newly-made class. */
lily_class *lily_new_class_by_seed(lily_symtab *symtab, const void *seed)
{
    lily_class_seed *class_seed = (lily_class_seed *)seed;
    lily_class *new_class = lily_new_class(symtab, class_seed->name);
    lily_type *type;

    /* If a class doesn't take generics (or isn't the generic class), then
        give it a default type.  */
    if (class_seed->generic_count != 0)
        type = NULL;
    else {
        /* A basic class? Make a quick default type for it. */
        type = lily_new_type(symtab, new_class);
        new_class->type = type;
    }

    new_class->type = type;
    new_class->generic_count = class_seed->generic_count;
    new_class->gc_marker = class_seed->gc_marker;
    new_class->flags = class_seed->flags;
    new_class->is_refcounted = class_seed->is_refcounted;
    new_class->eq_func = class_seed->eq_func;
    new_class->destroy_func = class_seed->destroy_func;
    new_class->import = symtab->active_import;
    new_class->dynaload_table = class_seed->dynaload_table;

    /* This is done so that the vm can & the flags of the class as a fast means
       of determining if something is refcounted. */
    if (new_class->is_refcounted == 0)
        new_class->flags |= VAL_IS_PRIMITIVE;

    return new_class;
}

#define SKIP_FLAGS \
    ~(TYPE_MAYBE_CIRCULAR | TYPE_IS_UNRESOLVED)

/*  lookup_type
    Determine if the current type exists in the symtab.

    Success: The type from the symtab is returned.
    Failure: NULL is returned. */
static lily_type *lookup_type(lily_symtab *symtab, lily_type *input_type)
{
    lily_type *iter_type = symtab->root_type;
    lily_type *ret = NULL;

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

    The symtab doesn't use this information at all. These are convenience
    things for the emitter and the vm. */
static void finalize_type(lily_type *input_type)
{
    if (input_type->subtypes) {
        int i;
        for (i = 0;i < input_type->subtype_count;i++) {
            lily_type *subtype = input_type->subtypes[i];
            if (subtype) {
                int flags = subtype->flags;
                if (flags & TYPE_MAYBE_CIRCULAR)
                    input_type->flags |= TYPE_MAYBE_CIRCULAR;
                if (flags & TYPE_IS_UNRESOLVED)
                    input_type->flags |= TYPE_IS_UNRESOLVED;
            }
        }
    }

    /* This gives emitter and vm an easy way to check if a type needs to be
       resolved or if it can used as-is. */
    if (input_type->cls->id == SYM_CLASS_GENERIC)
        input_type->flags |= TYPE_IS_UNRESOLVED;

    /* Any function can be a closure, and potentially close over something that
       is circular. Mark it as being possibly circular to be safe. */
    if (input_type->cls->id == SYM_CLASS_FUNCTION)
        input_type->flags |= TYPE_MAYBE_CIRCULAR;

    /* fixme: Properly go over enum classes to determine circularity. */
    if (input_type->cls->flags & CLS_ENUM_CLASS)
        input_type->flags |= TYPE_MAYBE_CIRCULAR;
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
            ((var_iter->flags & VAR_OUT_OF_SCOPE) == 0) &&
            strcmp(var_iter->name, name) == 0) {

            break;
            
        }
        var_iter = var_iter->next;
    }

    return var_iter;
}

static lily_import_entry *find_import(lily_import_entry *import,
        char *name)
{
    lily_import_link *link_iter = import->import_chain;
    lily_import_entry *result = NULL;
    while (link_iter) {
        char *as_name = link_iter->as_name;
        char *loadname = link_iter->entry->loadname;

        /* If it was imported like 'import x as y', then as_name will be
           non-null. In such a case, don't allow fallback access as 'x', just
           in case something else is imported with the name 'x'. */
        if ((as_name && strcmp(as_name, name) == 0) ||
            (as_name == NULL && loadname && strcmp(loadname, name) == 0)) {
            result = link_iter->entry;
            break;
        }

        link_iter = link_iter->next_import;
    }

    return result;
}

/*****************************************************************************/
/* Symtab initialization */
/*****************************************************************************/

lily_symtab *lily_new_symtab(lily_options *options,
        lily_import_entry *builtin_import)
{
    lily_symtab *symtab = lily_malloc(sizeof(lily_symtab));

    symtab->main_function = NULL;
    symtab->next_readonly_spot = 0;
    symtab->next_class_id = 0;
    symtab->main_var = NULL;
    symtab->old_function_chain = NULL;
    symtab->literals = NULL;
    symtab->function_ties = NULL;
    symtab->foreign_ties = NULL;
    symtab->builtin_import = builtin_import;
    symtab->active_import = builtin_import;
    symtab->root_type = NULL;
    symtab->generic_class = NULL;
    symtab->generic_type_start = NULL;
    symtab->old_class_chain = NULL;

    lily_init_builtin_package(symtab, builtin_import);

    return symtab;
}

/*****************************************************************************/
/* Symtab teardown                                                           */
/*****************************************************************************/

/** Symtab free-ing **/

/*  free_vars
    Free a given linked list of vars. */
void free_vars(lily_symtab *symtab, lily_var *var)
{
    lily_var *var_next;

    while (var) {
        var_next = var->next;

        lily_free(var->name);
        lily_free(var);

        var = var_next;
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

        lily_free(prop_iter->name);
        lily_free(prop_iter);

        prop_iter = next_prop;
    }
}

static void free_classes(lily_symtab *symtab, lily_class *class_iter)
{
    while (class_iter) {
        if (class_iter->properties != NULL)
            free_properties(symtab, class_iter);

        if (class_iter->call_chain != NULL)
            free_vars(symtab, class_iter->call_chain);

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

static void free_ties(lily_symtab *symtab, lily_tie *tie_iter)
{
    lily_tie *tie_next;

    while (tie_iter) {
        tie_next = tie_iter->next;
        lily_class *tie_cls = tie_iter->type->cls;
        /* Variant classes must be skipped, because their deref-er is a tuple
           deref-er. This is bad, because variants that are created as literals
           do not take inner values (and are represented as an integer).
           Everything else? Yeah, blow it away. */
        if ((tie_cls->flags & CLS_VARIANT_CLASS) == 0)
            lily_deref_raw(tie_iter->type,
                    tie_iter->value);

        lily_free(tie_iter);
        tie_iter = tie_next;
    }
}

/*  Destroy everything inside of the symtab. This happens after the vm is done
    being torn down, so it is safe to blast literals, defined functions, types,
    etc. */
void lily_free_symtab(lily_symtab *symtab)
{
    /* Ties have to come first because deref functions rely on type and class
       information. */
    free_ties(symtab, symtab->literals);
    free_ties(symtab, symtab->function_ties);

    free_classes(symtab, symtab->old_class_chain);
    free_vars(symtab, symtab->old_function_chain);

    lily_import_entry *import_iter = symtab->builtin_import;
    while (import_iter) {
        free_classes(symtab, import_iter->class_chain);
        free_vars(symtab, import_iter->var_chain);

        import_iter = import_iter->root_next;
    }

    /* __main__ requires a special teardown because it doesn't allocate names
       for debug, and its code is a shallow copy of emitter's code block. */
    lily_function_val *main_function = symtab->main_function;
    lily_free(main_function->reg_info);
    lily_free(main_function);

    lily_type *type, *type_temp;
    type = symtab->root_type;
    while (type != NULL) {
        type_temp = type->next;

        lily_free(type->subtypes);
        lily_free(type);
        type = type_temp;
    }

    /* Symtab does not attempt to free 'foreign_ties'. This is because the vm
       frees the foreign ties as it loads them (since there are so few).
       Therefore, foreign_ties should always be NULL at this point. */

    lily_free(symtab);
}

/*****************************************************************************/
/* Exported functions                                                        */
/*****************************************************************************/

/* These next three are used to get an integer, double, or string literal.
   They first look to see of the symtab has a literal with that value, then
   attempt to create it if there isn't one. */

lily_tie *lily_get_integer_literal(lily_symtab *symtab, int64_t int_val)
{
    lily_tie *lit, *ret;
    ret = NULL;
    lily_class *integer_cls = symtab->integer_class;
    lily_type *want_type = integer_cls->type;

    for (lit = symtab->literals;lit != NULL;lit = lit->next) {
        if (lit->type == want_type && lit->value.integer == int_val) {
            ret = lit;
            break;
        }
    }

    if (ret == NULL) {
        ret = make_new_literal(symtab, integer_cls);
        ret->value.integer = int_val;
        ret->flags |= VAL_IS_PRIMITIVE;
    }

    return ret;
}

lily_tie *lily_get_double_literal(lily_symtab *symtab, double dbl_val)
{
    lily_tie *lit, *ret;
    ret = NULL;
    lily_class *double_cls = symtab->double_class;
    lily_type *want_type = double_cls->type;

    for (lit = symtab->literals;lit != NULL;lit = lit->next) {
        if (lit->type == want_type && lit->value.doubleval == dbl_val) {
            ret = lit;
            break;
        }
    }

    if (ret == NULL) {
        ret = make_new_literal(symtab, double_cls);
        ret->value.doubleval = dbl_val;
        ret->flags |= VAL_IS_PRIMITIVE;
    }

    return ret;
}

lily_tie *lily_get_string_literal(lily_symtab *symtab, char *want_string)
{
    lily_tie *lit, *ret;
    ret = NULL;
    int want_string_len = strlen(want_string);

    for (lit = symtab->literals;lit;lit = lit->next) {
        if (lit->type->cls->id == SYM_CLASS_STRING) {
            if (lit->value.string->size == want_string_len &&
                strcmp(lit->value.string->string, want_string) == 0) {
                ret = lit;
                break;
            }
        }
    }

    if (ret == NULL) {
        lily_class *cls = symtab->string_class;
        char *string_buffer = lily_malloc((want_string_len + 1) * sizeof(char));
        lily_string_val *sv = lily_malloc(sizeof(lily_string_val));

        strcpy(string_buffer, want_string);
        sv->string = string_buffer;
        sv->size = want_string_len;
        sv->refcount = 1;

        ret = make_new_literal(symtab, cls);
        ret->value.string = sv;
    }

    return ret;
}

lily_tie *lily_get_bytestring_literal(lily_symtab *symtab,
        char *want_string, int want_string_len)
{
    lily_tie *lit, *ret;
    ret = NULL;

    for (lit = symtab->literals;lit;lit = lit->next) {
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
        lily_class *cls = symtab->bytestring_class;
        char *buffer = lily_malloc(want_string_len * sizeof(char));
        lily_string_val *bv = lily_malloc(sizeof(lily_string_val));

        memcpy(buffer, want_string, want_string_len);
        bv->string = buffer;
        bv->size = want_string_len;
        bv->refcount = 1;

        ret = make_new_literal(symtab, cls);
        ret->value.string = bv;
    }

    return ret;
}

/*  lily_get_variant_literal
    This function is like the other literal getters, except that it's called
    for empty variant classes. An empty variant class will always be the same,
    so an empty literal is passed around for the value.
    Otherwise the interpreter would have to create a bunch of nothings with the
    same value, and that would be rather silly. :) */
lily_tie *lily_get_variant_literal(lily_symtab *symtab,
        lily_type *variant_type)
{
    lily_tie *lit_iter, *ret;
    ret = NULL;

    for (lit_iter = symtab->literals;
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
        ret->flags |= VAL_IS_PRIMITIVE;
    }

    return ret;
}

static void tie_function(lily_symtab *symtab, lily_var *func_var,
        lily_function_val *func_val, lily_import_entry *import)
{
    lily_tie *tie = lily_malloc(sizeof(lily_tie));

    /* This is done so that lily_debug can print line numbers. */
    func_val->line_num = func_var->line_num;
    func_val->import = import;

    tie->type = func_var->type;
    tie->value.function = func_val;
    tie->reg_spot = func_var->reg_spot;
    tie->flags = ITEM_TYPE_TIE;

    tie->next = symtab->function_ties;
    symtab->function_ties = tie;
}

void lily_tie_builtin(lily_symtab *symtab, lily_var *func_var,
        lily_function_val *func_val)
{
    tie_function(symtab, func_var, func_val, symtab->builtin_import);
}

void lily_tie_function(lily_symtab *symtab, lily_var *func_var,
        lily_function_val *func_val)
{
    tie_function(symtab, func_var, func_val, symtab->active_import);
}

void lily_tie_value(lily_symtab *symtab, lily_var *var, lily_value *value)
{
    lily_tie *tie = lily_malloc(sizeof(lily_tie));

    tie->type = var->type;
    tie->value = value->value;
    tie->reg_spot = var->reg_spot;
    tie->flags = ITEM_TYPE_TIE;
    tie->next = symtab->foreign_ties;
    symtab->foreign_ties = tie;
}

/*  Attempt to locate a class. The semantics differ depending on if 'import' is
    NULL or not.

    import == NULL:
        Search through the current import and the builtin import. If both of
        those fail, then check for the name being a generic ('A', for example.
    import != NULL:
        Search only through the import given. */
lily_class *lily_find_class(lily_symtab *symtab, lily_import_entry *import,
        const char *name)
{
    uint64_t shorthash = shorthash_for_name(name);
    lily_class *result;

    if (import == NULL) {
        result = find_class(symtab->builtin_import->class_chain, name,
                shorthash);
        if (result == NULL) {
            result = find_class(symtab->active_import->class_chain, name,
                    shorthash);
            if (result == NULL) {
                /* The parser wants to be able to find classes by name...but it
                   would be a waste to have lots of classes that never actually
                   get used. The parser -really- just wants to get the type,
                   so... */
                if (name[1] == '\0') {
                    lily_type *generic_type = lookup_generic(symtab, name);
                    if (generic_type) {
                        result = symtab->generic_class;
                        result->type = generic_type;
                    }
                }
            }
        }
    }
    else
        result = find_class(import->class_chain, name, shorthash);

    return result;
}

/*  lily_find_class_callable
    Check if a class has a given function within it. If it doesn't, see if the
    class comes with a 'dynaload_table' that defines more functions. If it has a
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
    if (method_var == symtab->active_import->var_chain)
        symtab->active_import->var_chain = method_var->next;

    method_var->next = cls->call_chain;
    cls->call_chain = method_var;
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

/*  Attempt to locate a var. The semantics differ depending on if 'import' is
    NULL or not.

    import == NULL:
        Search through the current import and the builtin import.
    import != NULL:
        Search only through the import given. */
lily_var *lily_find_var(lily_symtab *symtab, lily_import_entry *import,
        char *name)
{
    uint64_t shorthash = shorthash_for_name(name);
    lily_var *result;

    if (import == NULL) {
        result = find_var(symtab->builtin_import->var_chain, name,
                    shorthash);
        if (result == NULL)
            result = find_var(symtab->active_import->var_chain, name, shorthash);
    }
    else
        result = find_var(import->var_chain, name, shorthash);

    return result;
}

/*  lily_hide_block_vars
    This function is called by emitter when a block goes out of scope. Vars
    until 'var_stop' are now out of scope. But...don't delete them because the
    emitter will need to know their type info later. */
void lily_hide_block_vars(lily_symtab *symtab, lily_var *var_stop)
{
    lily_var *var_iter = symtab->active_import->var_chain;
    while (var_iter != var_stop) {
        var_iter->flags |= VAR_OUT_OF_SCOPE;
        var_iter = var_iter->next;
    }
}

/*  lily_build_type
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
lily_type *lily_build_type(lily_symtab *symtab, lily_class *cls,
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
        lily_type *save_root = symtab->root_type;
        lily_type *new_type = lily_new_type(symtab, fake_type.cls);
        lily_type **new_subtypes = lily_malloc(entries_to_use *
                sizeof(lily_type *));

        memcpy(new_type, &fake_type, sizeof(lily_type));
        memcpy(new_subtypes, subtypes + offset, sizeof(lily_type *) * entries_to_use);
        new_type->subtypes = new_subtypes;
        new_type->subtype_count = entries_to_use;

        /* This is necessary because the first memcpy wipes out the 'next'
           field, which was the root before the new type was added. */
        new_type->next = save_root;
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
    lily_prop_entry *entry = lily_malloc(sizeof(lily_prop_entry));
    char *entry_name = lily_malloc(strlen(name) + 1);

    strcpy(entry_name, name);

    entry->flags = flags | ITEM_TYPE_PROPERTY;
    entry->name = entry_name;
    entry->type = type;
    entry->name_shorthash = shorthash_for_name(entry_name);
    entry->next = NULL;
    entry->id = cls->prop_count;
    entry->cls = cls;
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
                cls->gc_marker = symtab->tuple_class->gc_marker;
        }
        else
            /* Each instance of a generic class may/may not be circular depending
               on what it's given. */
            cls->gc_marker = symtab->tuple_class->gc_marker;

        cls->destroy_func = symtab->tuple_class->destroy_func;
        cls->eq_func = symtab->tuple_class->eq_func;
    }
    else {
        /* Enum classes have the same layout as 'any', and should thus use what
           'any' uses for things. */
        cls->gc_marker = symtab->any_class->gc_marker;
        cls->eq_func = symtab->any_class->eq_func;
        cls->destroy_func = symtab->any_class->destroy_func;
    }

    if (cls != symtab->old_class_chain) {
        lily_class *class_iter = symtab->active_import->class_chain;
        lily_class *class_next;

        while (class_iter != cls) {
            class_next = class_iter->next;
            class_iter->next = symtab->old_class_chain;
            symtab->old_class_chain = class_iter;
            class_iter = class_next;
        }

        symtab->active_import->class_chain = cls;
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
            lily_type *new_type = lily_new_type(symtab, symtab->generic_class);
            new_type->flags = TYPE_IS_UNRESOLVED;
            new_type->generic_pos = i;

            /* lily_new_type makes the newly-created type the most recent one.
               However, it's simpler if the generic types (A, B, C, etc.) are
               all grouped together. */
            symtab->root_type = symtab->root_type->next;

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
    lily_class *target_class = symtab->active_import->class_chain;
    lily_type *type = lily_new_type(symtab, target_class);

    if (target_class->generic_count != 0) {
        int count = target_class->generic_count;

        type->subtypes = lily_malloc(count * sizeof(lily_type *));

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
    }
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

    cls->flags |= CLS_VARIANT_CLASS | ITEM_TYPE_VARIANT_CLASS;
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
        lily_type *type = lily_new_type(symtab, variant_class);

        type->cls = variant_class;
        /* Anything that doesn't take parameters gets a default type. */
        variant_class->type = type;

        variant_class->variant_type = type;
        /* Empty variants are represented as integers, and won't need to be
           marked through. */
        variant_class->eq_func = symtab->integer_class->eq_func;
        variant_class->is_refcounted = 0;
    }
    else {
        variant_class->variant_type = variant_type;
        /* The only difference between a tuple and a variant with args is that
           the variant has a variant type instead of a tuple one. */
        variant_class->gc_marker = symtab->tuple_class->gc_marker;
        variant_class->eq_func = symtab->tuple_class->eq_func;
        variant_class->destroy_func = symtab->tuple_class->destroy_func;
    }
}

void lily_finish_enum_class(lily_symtab *symtab, lily_class *enum_class,
        int is_scoped, lily_type *enum_type)
{
    int i, variant_count = 0;
    lily_class *class_iter = symtab->active_import->class_chain;
    while (class_iter != enum_class) {
        variant_count++;
        class_iter = class_iter->next;
    }

    lily_class **members = lily_malloc(variant_count * sizeof(lily_class *));

    for (i = 0, class_iter = symtab->active_import->class_chain;
         i < variant_count;
         i++, class_iter = class_iter->next)
        members[i] = class_iter;

    enum_class->variant_type = enum_type;
    enum_class->variant_members = members;
    enum_class->variant_size = variant_count;
    enum_class->flags |= CLS_ENUM_CLASS;
    enum_class->gc_marker = symtab->any_class->gc_marker;
    enum_class->eq_func = symtab->any_class->eq_func;
    enum_class->destroy_func = symtab->any_class->destroy_func;

    if (is_scoped) {
        enum_class->flags |= CLS_ENUM_IS_SCOPED;
        /* This removes the variants from symtab's classes, so that parser has
           to get them from the enum class. */
        symtab->active_import->class_chain = enum_class;
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

lily_import_entry *lily_find_import_anywhere(lily_symtab *symtab,
        char *name)
{
    lily_import_entry *entry_iter = symtab->builtin_import;

    while (entry_iter) {
        if (entry_iter->loadname &&
            strcmp(entry_iter->loadname, name) == 0)
            break;

        entry_iter = entry_iter->root_next;
    }

    return entry_iter;
}

/*  Attempt to locate an import. The semantics differ depending on if 'import'
    is NULL or not.

    import == NULL:
        Search through the current import and the builtin import.
    import != NULL:
        Search only through the import given. */
lily_import_entry *lily_find_import(lily_symtab *symtab,
        lily_import_entry *import, char *name)
{
    lily_import_entry *result;
    if (import == NULL) {
        result = find_import(symtab->active_import,
                name);
        if (result == NULL)
            result = find_import(symtab->builtin_import, name);
    }
    else
        result = find_import(import, name);

    return result;
}
