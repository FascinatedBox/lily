#include <ctype.h>
#include <string.h>

#include "lily_alloc.h"
#include "lily_symtab.h"
#include "lily_pkg_builtin.h"
#include "lily_seed.h"

#include "lily_value.h"

/***
 *      ____       _
 *     / ___|  ___| |_ _   _ _ __
 *     \___ \ / _ \ __| | | | '_ \
 *      ___) |  __/ |_| |_| | |_) |
 *     |____/ \___|\__|\__,_| .__/
 *                          |_|
 */

lily_symtab *lily_new_symtab(lily_import_entry *builtin_import)
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
    symtab->generic_class = NULL;
    symtab->old_class_chain = NULL;

    /* Builtin classes are established by this function. */
    lily_init_builtin_package(symtab, builtin_import);

    return symtab;
}

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

        lily_type *type_iter = class_iter->all_subtypes;
        lily_type *type_next;
        while (type_iter) {
            type_next = type_iter->next;
            lily_free(type_iter->subtypes);
            lily_free(type_iter);
            type_iter = type_next;
        }

        lily_free(class_iter->name);

        if (class_iter->flags & CLS_ENUM_IS_SCOPED) {
            /* Scoped enums pull their variants from the symtab's class chain so
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
        lily_deref_raw(tie_iter->type, tie_iter->value);
        lily_free(tie_iter);
        tie_iter = tie_next;
    }
}

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

    lily_free(symtab);
}

/***
 *      _     _ _                 _
 *     | |   (_) |_ ___ _ __ __ _| |___
 *     | |   | | __/ _ \ '__/ _` | / __|
 *     | |___| | ||  __/ | | (_| | \__ \
 *     |_____|_|\__\___|_|  \__,_|_|___/
 *
 */

/** These functions are used to grab a new literal value. Each getter will try
    to get an existing literal of the given value before making a new one.
    The only one of interest is the variant 'literal'. Some variants like the
    None of an Option do not need a unique value. So, instead, they all share
    a literal tagged as a None (but which is just an integer). **/

static lily_tie *make_new_literal(lily_symtab *symtab, lily_class *cls)
{
    lily_tie *lit = lily_malloc(sizeof(lily_tie));

    /* Literal values always have a default type, so this is safe. */
    lit->type = cls->type;

    lit->flags = ITEM_TYPE_TIE;
    lit->reg_spot = symtab->next_readonly_spot;
    symtab->next_readonly_spot++;

    lit->next = symtab->literals;
    symtab->literals = lit;

    return lit;
}

lily_tie *lily_get_boolean_literal(lily_symtab *symtab, int64_t int_val)
{
    lily_tie *lit, *ret;
    ret = NULL;
    lily_class *boolean_cls = symtab->boolean_class;
    lily_type *want_type = boolean_cls->type;

    for (lit = symtab->literals;lit != NULL;lit = lit->next) {
        if (lit->type == want_type && lit->value.integer == int_val) {
            ret = lit;
            break;
        }
    }

    if (ret == NULL) {
        ret = make_new_literal(symtab, boolean_cls);
        ret->value.integer = int_val;
        ret->flags |= VAL_IS_PRIMITIVE;
    }

    return ret;
}

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

/***
 *     __     __
 *     \ \   / /_ _ _ __ ___
 *      \ \ / / _` | '__/ __|
 *       \ V / (_| | |  \__ \
 *        \_/ \__,_|_|  |___/
 *
 */

/** Symtab is responsible for creating vars. However, emitter is the one that
    knows about register positions and where the var will go. So the symtab may
    make the vars but it that's about it. **/

/* This gets (up to) the first 8 bytes of a name and puts it into a numeric
   value. The numeric value is compared before comparing names to speed things
   up just a bit. */
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

/* Create a new var, but leave it to the caller to link it somewhere. */
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

/* Create a new var that is immediately added to the current import. */
lily_var *lily_new_raw_var(lily_symtab *symtab, lily_type *type,
        const char *name)
{
    lily_var *var = lily_new_raw_unlinked_var(symtab, type, name);

    var->next = symtab->active_import->var_chain;
    symtab->active_import->var_chain = var;

    return var;
}

static lily_var *find_var(lily_var *var_iter, char *name, uint64_t shorthash)
{
    while (var_iter != NULL) {
        /* TODO: Emitter marks vars as being out of scope so that it can grab
           their types later during function finalize. While that's fine, the
           vars shouldn't be left for the symtab to have to jump over. */
        if (var_iter->shorthash == shorthash &&
            ((var_iter->flags & VAR_OUT_OF_SCOPE) == 0) &&
            strcmp(var_iter->name, name) == 0) {

            break;
        }
        var_iter = var_iter->next;
    }

    return var_iter;
}

/* Try to find a var. If the given import is NULL, then search through both the
   current and builtin imports. For everything else, just search through the
   import given. */
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

/* Hide all vars that occur until 'var_stop'. */
void lily_hide_block_vars(lily_symtab *symtab, lily_var *var_stop)
{
    lily_var *var_iter = symtab->active_import->var_chain;
    while (var_iter != var_stop) {
        var_iter->flags |= VAR_OUT_OF_SCOPE;
        var_iter = var_iter->next;
    }
}

/***
 *      _____ _
 *     |_   _(_) ___  ___
 *       | | | |/ _ \/ __|
 *       | | | |  __/\__ \
 *       |_| |_|\___||___/
 *
 */

/** Ties are used to associate some piece of data with a register spot. The vm
    is responsible for loading these ties into somewhere appropriate during the
    next vm prep phase. **/

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

/***
 *       ____ _
 *      / ___| | __ _ ___ ___  ___  ___
 *     | |   | |/ _` / __/ __|/ _ \/ __|
 *     | |___| | (_| \__ \__ \  __/\__ \
 *      \____|_|\__,_|___/___/\___||___/
 *
 */

static lily_type *make_new_type(lily_class *);

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
        type = make_new_type(new_class);
        new_class->type = type;
        new_class->all_subtypes = type;
    }

    new_class->type = type;
    new_class->is_builtin = 1;
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

/* This creates a new class entity. This entity is used for, well, more than it
   should be. The entity is going to be either an enum, a variant, or a
   user-defined class. The class is assumed to be refcounted, because it usually
   is.
   The new class is automatically linked up to the current import. No default
   type is created, in case the newly-made class ends up needing generics. */
lily_class *lily_new_class(lily_symtab *symtab, char *name)
{
    lily_class *new_class = lily_malloc(sizeof(lily_class));
    char *name_copy = lily_malloc(strlen(name) + 1);

    strcpy(name_copy, name);

    new_class->flags = CLS_IS_CURRENT;
    new_class->is_refcounted = 1;
    new_class->is_builtin = 0;
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
    new_class->all_subtypes = NULL;

    new_class->id = symtab->next_class_id;
    symtab->next_class_id++;

    new_class->next = symtab->active_import->class_chain;
    symtab->active_import->class_chain = new_class;

    return new_class;
}

/* This creates a new type but doesn't add it to the 'all_subtypes' field of the
   given class (that's left for the caller to do). */
static lily_type *make_new_type(lily_class *cls)
{
    lily_type *new_type = lily_malloc(sizeof(lily_type));
    new_type->cls = cls;
    new_type->flags = 0;
    new_type->generic_pos = 0;
    new_type->subtype_count = 0;
    new_type->subtypes = NULL;
    new_type->next = NULL;

    return new_type;
}

static lily_type *lookup_generic(lily_symtab *symtab, const char *name)
{
    int id = name[0] - 'A';
    lily_type *type_iter = symtab->generic_class->all_subtypes;

    while (type_iter) {
        if (type_iter->generic_pos == id) {
            if (type_iter->flags & TYPE_HIDDEN_GENERIC)
                type_iter = NULL;

            break;
        }

        type_iter = type_iter->next;
    }

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


/* Try to find a class. If 'import' is NULL, then search through both the
   current import AND the builtin import. In all other cases, search just the
   import given. */
lily_class *lily_find_class(lily_symtab *symtab, lily_import_entry *import,
        const char *name)
{
    uint64_t shorthash = shorthash_for_name(name);
    lily_class *result;

    if (import == NULL) {
        if (name[1] != '\0') {
            result = find_class(symtab->builtin_import->class_chain, name,
                    shorthash);
            if (result == NULL)
                result = find_class(symtab->active_import->class_chain, name,
                        shorthash);
        }
        else {
            lily_type *generic_type = lookup_generic(symtab, name);
            if (generic_type) {
                /* It's rather silly to make a different class for each generic
                   type. Instead, write out whatever generic type was found as
                   the default type. The generic class is written to have no
                   subtypes, so this is okay.
                   ts and other modules always special case the generic class,
                   and won't be bothered by this little trick. */
                result = symtab->generic_class;
                result->type = generic_type;
            }
            else
                result = NULL;
        }
    }
    else
        result = find_class(import->class_chain, name, shorthash);

    return result;
}

/* Try to find a method within the class given. The given class is search first,
   then any parents of the class. */
lily_var *lily_find_method(lily_class *cls, char *name)
{
    lily_var *iter;
    uint64_t shorthash = shorthash_for_name(name);

    for (iter = cls->call_chain;iter != NULL;iter = iter->next) {
        if (iter->shorthash == shorthash && strcmp(iter->name, name) == 0)
            break;
    }

    if (iter == NULL && cls->parent)
        iter = lily_find_method(cls->parent, name);

    return iter;
}

/* Add a var as a method to the current class. The var should be at the top of
   whatever list it is in, since it is to be taken out of it's current list. */
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

/* Try to find a property with a name in a class. The parent class(es), if any,
   are tries as a fallback if unable to find it in the given class. */
lily_prop_entry *lily_find_property(lily_class *cls, char *name)
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
        ret = lily_find_property(cls->parent, name);

    return ret;
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
            (as_name == NULL && strcmp(loadname, name) == 0)) {
            result = link_iter->entry;
            break;
        }

        link_iter = link_iter->next_import;
    }

    return result;
}

/* Create a new property and add it into the class. As a convenience, the
   newly-made property is also returned. */
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

/* This is called when a class is being exited. It sets the various callbacks on
   the class (mark, eq, and others). */
void lily_finish_class(lily_symtab *symtab, lily_class *cls)
{
    if ((cls->flags & CLS_IS_ENUM) == 0) {
        /* If a class is CLS_ALWAYS_MARK, then the class has something that
           definitely needs a marker.
           Classes with 1+ generics are simply not trusted, ever, as one of the
           generic types could be replaced with, say, type 'any'. */
        if (cls->flags & CLS_ALWAYS_MARK || cls->generic_count != 0)
            cls->gc_marker = symtab->tuple_class->gc_marker;

        cls->destroy_func = symtab->tuple_class->destroy_func;
        cls->eq_func = symtab->tuple_class->eq_func;
    }
    else {
        /* Enums have the same layout as 'any', and should thus use what 'any'
           uses for things. */
        cls->gc_marker = symtab->any_class->gc_marker;
        cls->eq_func = symtab->any_class->eq_func;
        cls->destroy_func = symtab->any_class->destroy_func;
    }

    cls->flags &= ~CLS_IS_CURRENT;
}

/***
 *      _____
 *     | ____|_ __  _   _ _ __ ___  ___
 *     |  _| | '_ \| | | | '_ ` _ \/ __|
 *     | |___| | | | |_| | | | | | \__ \
 *     |_____|_| |_|\__,_|_| |_| |_|___/
 *
 */

/* This creates a new variant called 'name' and installs it into 'enum_cls'. */
lily_class *lily_new_variant(lily_symtab *symtab, lily_class *enum_cls,
        char *name)
{
    lily_class *cls = lily_new_class(symtab, name);

    cls->flags |= CLS_IS_VARIANT | ITEM_TYPE_VARIANT;
    cls->parent = enum_cls;

    return cls;
}

/* Scoped variants are stored within the enum they're part of. This will try to
   find a variant stored within 'enum_cls'. */
lily_class *lily_find_scoped_variant(lily_class *enum_cls, char *name)
{
    int i;
    uint64_t shorthash = shorthash_for_name(name);
    lily_class *ret = NULL;

    for (i = 0;i < enum_cls->variant_size;i++) {
        lily_class *variant_cls = enum_cls->variant_members[i];
        if (variant_cls->shorthash == shorthash &&
            strcmp(variant_cls->name, name) == 0) {
            ret = variant_cls;
        }
    }

    return ret;
}

/* The vm will create enums as a value with a certain number of sub values. The
   number of subvalues necessary is the maximum number of values that any one
   variant has. This function will walk through an enum's variants to find out
   how many slots that the vm will need to make. */
static uint16_t get_enum_slot_count(lily_class *enum_cls)
{
    int i;
    uint16_t result = 0;
    for (i = 0;i < enum_cls->variant_size;i++) {
        /* Variants that take 'arguments' will have a variant_type that is a
           function. Variants that don't take arguments don't need slots, and
           thus don't matter. */
        lily_type *vtype = enum_cls->variant_members[i]->variant_type;
        if (vtype->cls->id == SYM_CLASS_FUNCTION) {
            uint16_t num_subtypes = vtype->subtype_count - 1;
            if (num_subtypes > result)
                result = num_subtypes;
        }
    }

    return result;
}

/* This is called when an enum class has finished scanning the variant members.
   If the enum is to be scoped, then the enums are bound within it. This is also
   where some callbacks are set on the enum (gc, eq, etc.) */
void lily_finish_enum(lily_symtab *symtab, lily_class *enum_cls, int is_scoped,
        lily_type *enum_type)
{
    int i, variant_count = 0;
    lily_class *class_iter = symtab->active_import->class_chain;
    while (class_iter != enum_cls) {
        variant_count++;
        class_iter = class_iter->next;
    }

    lily_class **members = lily_malloc(variant_count * sizeof(lily_class *));

    /* The ordering is important here. This makes it so the first variant will
       get the lowest id and be at 0. It makes indexing in vm sensible. */
    for (i = 0, class_iter = symtab->active_import->class_chain;
         i < variant_count;
         i++, class_iter = class_iter->next) {
        members[variant_count - 1 - i] = class_iter;
        class_iter->variant_id = variant_count - 1 - i;
    }

    enum_cls->variant_type = enum_type;
    enum_cls->variant_members = members;
    enum_cls->variant_size = variant_count;
    enum_cls->flags |= CLS_IS_ENUM;
    enum_cls->flags &= ~CLS_IS_CURRENT;
    enum_cls->gc_marker = symtab->tuple_class->gc_marker;
    enum_cls->eq_func = symtab->tuple_class->eq_func;
    enum_cls->destroy_func = symtab->tuple_class->destroy_func;
    enum_cls->enum_slot_count = get_enum_slot_count(enum_cls);

    if (is_scoped) {
        enum_cls->flags |= CLS_ENUM_IS_SCOPED;
        /* This removes the variants from symtab's classes, so that parser has
           to get them from the enum. */
        symtab->active_import->class_chain = enum_cls;
    }
}

/***
 *      _   _      _
 *     | | | | ___| |_ __   ___ _ __ ___
 *     | |_| |/ _ \ | '_ \ / _ \ '__/ __|
 *     |  _  |  __/ | |_) |  __/ |  \__ \
 *     |_| |_|\___|_| .__/ \___|_|  |___/
 *                  |_|
 */

/* This checks if a package named 'name' has been imported anywhere at all. This
   is used to prevent re-importing something that has already been imported (it
   can just be linked). */
lily_import_entry *lily_find_import_anywhere(lily_symtab *symtab,
        char *name)
{
    lily_import_entry *entry_iter = symtab->builtin_import;

    while (entry_iter) {
        if (strcmp(entry_iter->loadname, name) == 0)
            break;

        entry_iter = entry_iter->root_next;
    }

    return entry_iter;
}

/* Try to find an import named 'name' within the given import. If the given
   import is NULL, then both the current import AND the builtin import are
   searched. */
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


/* This...is called to 'fix' how many generics are available in the current
   class. As a 'neat' side-effect, it also sets how many generics that
   'decl_class' has. */
void lily_update_symtab_generics(lily_symtab *symtab, lily_class *decl_class,
        int count)
{
    /* The symtab special cases all types holding generic information so
       that they're unique, together, and in numerical order. */
    lily_type *type_iter = symtab->generic_class->all_subtypes;
    int i = 1, save_count = count;

    while (count) {
        type_iter->flags &= ~TYPE_HIDDEN_GENERIC;
        count--;
        if (type_iter->next == NULL && count) {
            lily_type *new_type = make_new_type(symtab->generic_class);
            new_type->flags = TYPE_IS_UNRESOLVED;
            new_type->generic_pos = i;

            /* It's much easier if generics are linked so that the higher number
               ones come further on. (A => B => C) instead of having the newest
               one be at the front. In fact, a couple of other modules poke the
               generics directly and rely on this ordering. */
            type_iter->next = new_type;
        }
        i++;
        type_iter = type_iter->next;
    }

    while (type_iter) {
        type_iter->flags |= TYPE_HIDDEN_GENERIC;
        type_iter = type_iter->next;
    }

    if (decl_class)
        decl_class->generic_count = save_count;
}

/* This makes 'sub_class' have 'super_class' as a parent. */
void lily_change_parent_class(lily_class *super_class, lily_class *sub_class)
{
    sub_class->parent = super_class;
    /* This must be copied over because the vm uses this number to determine
       how many slots to allocate for the class.
       Subclass properties can safely start after superclass properties because
       of single inheritance. */
    sub_class->prop_count = super_class->prop_count;
}
