#include <ctype.h>
#include <string.h>

#include "lily_symtab.h"
#include "lily_vm.h"

#include "lily_api_alloc.h"
#include "lily_api_value.h"
#include "lily_api_value_flags.h"

/***
 *      ____       _
 *     / ___|  ___| |_ _   _ _ __
 *     \___ \ / _ \ __| | | | '_ \
 *      ___) |  __/ |_| |_| | |_) |
 *     |____/ \___|\__|\__,_| .__/
 *                          |_|
 */

lily_symtab *lily_new_symtab(void)
{
    lily_symtab *symtab = lily_malloc(sizeof(lily_symtab));

    symtab->main_function = NULL;
    symtab->next_class_id = 0;
    symtab->main_var = NULL;
    symtab->old_function_chain = NULL;
    symtab->generic_class = NULL;
    symtab->old_class_chain = NULL;
    symtab->literals = lily_new_value_stack();

    return symtab;
}

void lily_set_first_package(lily_symtab *symtab, lily_package *package)
{
    symtab->builtin_module = package->first_module;
    symtab->active_module = package->first_module;
    symtab->first_package = package;
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
    lily_named_sym *prop_iter = cls->members;
    lily_named_sym *next_prop;
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
        lily_free(class_iter->name);

        if (class_iter->flags & CLS_IS_VARIANT) {
            lily_class *class_next = class_iter->next;
            lily_free(class_iter);
            class_iter = class_next;
            continue;
        }

        if (class_iter->members != NULL)
            free_properties(symtab, class_iter);

        lily_type *type_iter = class_iter->all_subtypes;
        lily_type *type_next;
        while (type_iter) {
            type_next = type_iter->next;
            lily_free(type_iter->subtypes);
            lily_free(type_iter);
            type_iter = type_next;
        }

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

static void free_literals(lily_value_stack *literals)
{
    while (lily_vs_pos(literals)) {
        lily_literal *lit = (lily_literal *)lily_vs_pop(literals);

        /* Literals are marked where their refcount won't be adjusted during
           the vm's run. Any literal that isn't primitive will have 1 ref, and
           can be destroyed by sending it to deref. */
        if ((lit->flags &
            (VAL_IS_BOOLEAN | VAL_IS_INTEGER | VAL_IS_DOUBLE)) == 0) {
            lit->flags |= VAL_IS_DEREFABLE;
            lily_deref((lily_value *)lit);
        }

        lily_free(lit);
    }

    lily_free_value_stack(literals);
}

void lily_free_symtab(lily_symtab *symtab)
{
    free_literals(symtab->literals);

    free_classes(symtab, symtab->old_class_chain);
    free_vars(symtab, symtab->old_function_chain);

    lily_package *package_iter = symtab->first_package;
    while (package_iter) {
        lily_module_entry *module_iter = package_iter->first_module;
        while (module_iter) {
            free_classes(symtab, module_iter->class_chain);
            free_vars(symtab, module_iter->var_chain);

            module_iter = module_iter->root_next;
        }
        package_iter = package_iter->root_next;
    }

    /* __main__ requires a special teardown because it doesn't allocate names
       for debug, and its code is a shallow copy of emitter's code block. */
    lily_function_val *main_function = symtab->main_function;
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
    a literal tagged as a None (but which is just an integer).
    Storing of (defined) functions is also here, because a function cannot be
    altered once it's defined. **/

/* Literals take advantage of lily_value having extra padding in it. That extra
   padding will have the index of the next literal of that kind. The only
   trouble is finding the first one with the given flag to start with. */
static lily_literal *first_lit_of(lily_value_stack *vs, int to_find)
{
    int stop = lily_vs_pos(vs);
    int i;

    for (i = 0;i < stop;i++) {
        lily_literal *lit = (lily_literal *)lily_vs_nth(vs, i);
        if (lit->flags & to_find)
            return lit;
    }

    return NULL;
}

lily_literal *lily_get_integer_literal(lily_symtab *symtab, int64_t int_val)
{
    lily_literal *iter = first_lit_of(symtab->literals, VAL_IS_INTEGER);

    while (iter) {
        if (iter->value.integer == int_val)
            return iter;

        int next = iter->next_index;

        if (next == 0)
            break;
        else
            iter = (lily_literal *)lily_vs_nth(symtab->literals, next);
    }

    if (iter)
        iter->next_index = lily_vs_pos(symtab->literals);

    lily_literal *v = (lily_literal *)lily_new_empty_value();
    v->flags = VAL_IS_INTEGER;
    v->reg_spot = lily_vs_pos(symtab->literals);
    v->next_index = 0;
    v->value.integer = int_val;

    lily_vs_push(symtab->literals, (lily_value *)v);
    return (lily_literal *)v;
}

lily_literal *lily_get_double_literal(lily_symtab *symtab, double dbl_val)
{
    lily_literal *iter = first_lit_of(symtab->literals, VAL_IS_DOUBLE);

    while (iter) {
        if (iter->value.doubleval == dbl_val)
            return iter;

        int next = iter->next_index;

        if (next == 0)
            break;
        else
            iter = (lily_literal *)lily_vs_nth(symtab->literals, next);
    }

    if (iter)
        iter->next_index = lily_vs_pos(symtab->literals);

    lily_literal *v = (lily_literal *)lily_new_empty_value();
    v->flags = VAL_IS_DOUBLE;
    v->reg_spot = lily_vs_pos(symtab->literals);
    v->next_index = 0;
    v->value.doubleval = dbl_val;

    lily_vs_push(symtab->literals, (lily_value *)v);
    return (lily_literal *)v;
}

lily_literal *lily_get_bytestring_literal(lily_symtab *symtab,
        const char *want_string, int len)
{
    lily_literal *iter = first_lit_of(symtab->literals, VAL_IS_BYTESTRING);

    while (iter) {
        if (iter->value.string->size == len &&
            memcmp(iter->value.string->string, want_string, len) == 0)
            return iter;

        int next = iter->next_index;

        if (next == 0)
            break;
        else
            iter = (lily_literal *)lily_vs_nth(symtab->literals, next);
    }

    if (iter)
        iter->next_index = lily_vs_pos(symtab->literals);

    lily_literal *v = (lily_literal *)lily_new_empty_value();
    v->flags = VAL_IS_BYTESTRING;
    v->reg_spot = lily_vs_pos(symtab->literals);
    v->value.string = lily_new_raw_string_sized(want_string, len);
    /* Manual ref, because the string isn't being moved/assigned anywhere. */
    v->value.string->refcount++;
    v->next_index = 0;

    lily_vs_push(symtab->literals, (lily_value *)v);
    return (lily_literal *)v;
}

lily_literal *lily_get_string_literal(lily_symtab *symtab,
        const char *want_string)
{
    lily_literal *iter = first_lit_of(symtab->literals, VAL_IS_STRING);
    int want_string_len = strlen(want_string);

    while (iter) {
        if (iter->value.string->size == want_string_len &&
            strcmp(iter->value.string->string, want_string) == 0)
            return iter;

        int next = iter->next_index;

        if (next == 0)
            break;
        else
            iter = (lily_literal *)lily_vs_nth(symtab->literals, next);
    }

    if (iter)
        iter->next_index = lily_vs_pos(symtab->literals);

    lily_literal *v = (lily_literal *)lily_new_empty_value();
    v->flags = VAL_IS_STRING;
    v->reg_spot = lily_vs_pos(symtab->literals);
    v->next_index = 0;
    v->value.string = lily_new_raw_string(want_string);
    /* Manual ref, because the string isn't being moved/assigned anywhere. */
    v->value.string->refcount++;

    lily_vs_push(symtab->literals, (lily_value *)v);
    return (lily_literal *)v;
}

/* Literals and defined functions are both immutable, so they occupy the same
   general. This stores a function in their shared area. */
static void store_function(lily_symtab *symtab, lily_var *func_var,
        lily_function_val *func_val, lily_module_entry *module)
{
    /* This is done so that lily_debug can print line numbers. */
    func_val->line_num = func_var->line_num;
    func_val->module = module;

    lily_value *v = lily_new_empty_value();
    v->flags = VAL_IS_FUNCTION;
    v->value.function = func_val;

    lily_vs_push(symtab->literals, v);
}

void lily_store_builtin(lily_symtab *symtab, lily_var *func_var,
        lily_function_val *func_val)
{
    store_function(symtab, func_var, func_val, symtab->builtin_module);
}

void lily_store_function(lily_symtab *symtab, lily_var *func_var,
        lily_function_val *func_val)
{
    store_function(symtab, func_var, func_val, symtab->active_module);
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
    var->item_kind = ITEM_TYPE_VAR;
    var->flags = 0;
    strcpy(var->name, name);
    var->line_num = *symtab->lex_linenum;

    var->shorthash = shorthash_for_name(name);
    var->type = type;
    var->next = NULL;
    var->parent = NULL;

    return var;
}

/* Create a new var that is immediately added to the current module. */
lily_var *lily_new_raw_var(lily_symtab *symtab, lily_type *type,
        const char *name)
{
    lily_var *var = lily_new_raw_unlinked_var(symtab, type, name);

    var->next = symtab->active_module->var_chain;
    symtab->active_module->var_chain = var;

    return var;
}

static lily_var *find_var(lily_var *var_iter, const char *name,
        uint64_t shorthash)
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

/* Try to find a var. If the given module is NULL, then search through both the
   current and builtin modules. For everything else, just search through the
   module given. */
lily_var *lily_find_var(lily_symtab *symtab, lily_module_entry *module,
        const char *name)
{
    uint64_t shorthash = shorthash_for_name(name);
    lily_var *result;

    if (module == NULL) {
        result = find_var(symtab->builtin_module->var_chain, name,
                    shorthash);
        if (result == NULL)
            result = find_var(symtab->active_module->var_chain, name, shorthash);
    }
    else
        result = find_var(module->var_chain, name, shorthash);

    return result;
}

/* Hide all vars that occur until 'var_stop'. */
void lily_hide_block_vars(lily_symtab *symtab, lily_var *var_stop)
{
    lily_var *var_iter = symtab->active_module->var_chain;
    while (var_iter != var_stop) {
        var_iter->flags |= VAR_OUT_OF_SCOPE;
        var_iter = var_iter->next;
    }
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
lily_class *lily_new_raw_class(lily_symtab *symtab, const char *name)
{
    lily_class *new_class = lily_new_class(symtab, name);

    new_class->flags |= CLS_IS_BUILTIN;
    new_class->id = 0;
    symtab->next_class_id--;

    return new_class;
}

/* This creates a new class entity. This entity is used for, well, more than it
   should be. The entity is going to be either an enum, a variant, or a
   user-defined class. The class is assumed to be refcounted, because it usually
   is.
   The new class is automatically linked up to the current module. No default
   type is created, in case the newly-made class ends up needing generics. */
lily_class *lily_new_class(lily_symtab *symtab, const char *name)
{
    lily_class *new_class = lily_malloc(sizeof(lily_class));
    char *name_copy = lily_malloc(strlen(name) + 1);

    strcpy(name_copy, name);

    new_class->item_kind = 0;
    new_class->flags = 0;
    new_class->type = NULL;
    new_class->parent = NULL;
    new_class->shorthash = shorthash_for_name(name);
    new_class->name = name_copy;
    new_class->generic_count = 0;
    new_class->prop_count = 0;
    new_class->variant_members = NULL;
    new_class->members = NULL;
    new_class->module = symtab->active_module;
    new_class->all_subtypes = NULL;
    new_class->move_flags = VAL_IS_INSTANCE;
    new_class->dyna_start = 0;

    new_class->id = symtab->next_class_id;
    symtab->next_class_id++;

    new_class->next = symtab->active_module->class_chain;
    symtab->active_module->class_chain = new_class;

    return new_class;
}

/* Use this to create a new class that represents an enum. */
lily_class *lily_new_enum(lily_symtab *symtab, const char *name)
{
    lily_class *new_class = lily_new_class(symtab, name);
    new_class->flags |= CLS_IS_ENUM;
    new_class->move_flags = VAL_IS_ENUM;

    return new_class;
}

/* This creates a new type but doesn't add it to the 'all_subtypes' field of the
   given class (that's left for the caller to do). */
static lily_type *make_new_type(lily_class *cls)
{
    lily_type *new_type = lily_malloc(sizeof(lily_type));
    new_type->item_kind = ITEM_TYPE_TYPE;
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


/* Try to find a class. If 'module' is NULL, then search through both the
   current module AND the builtin module. In all other cases, search just the
   module given. */
lily_class *lily_find_class(lily_symtab *symtab, lily_module_entry *module,
        const char *name)
{
    uint64_t shorthash = shorthash_for_name(name);
    lily_class *result;

    if (module == NULL) {
        if (name[1] != '\0') {
            result = find_class(symtab->builtin_module->class_chain, name,
                    shorthash);
            if (result == NULL)
                result = find_class(symtab->active_module->class_chain, name,
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
        result = find_class(module->class_chain, name, shorthash);

    return result;
}

/* Does 'name' exist within 'cls' as either a var or a name? If so, return it.
   If not, then return NULL. */
lily_named_sym *lily_find_member(lily_class *cls, const char *name)
{
    lily_named_sym *ret = NULL;

    if (cls->members != NULL) {
        uint64_t shorthash = shorthash_for_name(name);
        lily_named_sym *sym_iter = cls->members;
        while (sym_iter) {
            if (sym_iter->name_shorthash == shorthash &&
                strcmp(sym_iter->name, name) == 0) {
                ret = (lily_named_sym *)sym_iter;
                break;
            }

            sym_iter = sym_iter->next;
        }
    }

    if (ret == NULL && cls->parent != NULL)
        ret = lily_find_member(cls->parent, name);

    return ret;
}

/* Try to find a method within the class given. The given class is search first,
   then any parents of the class. */
lily_var *lily_find_method(lily_class *cls, const char *name)
{
    lily_named_sym *sym = lily_find_member(cls, name);
    if (sym && sym->item_kind != ITEM_TYPE_VAR)
        sym = NULL;

    return (lily_var *)sym;
}

/* Search for a property within the current class, then upward through parent
   classes if there are any. */
lily_prop_entry *lily_find_property(lily_class *cls, const char *name)
{
    lily_named_sym *sym = lily_find_member(cls, name);
    if (sym && sym->item_kind != ITEM_TYPE_PROPERTY)
        sym = NULL;

    return (lily_prop_entry *)sym;
}

/* Add a var as a method to the current class. The var should be at the top of
   whatever list it is in, since it is to be taken out of it's current list. */
void lily_add_class_method(lily_symtab *symtab, lily_class *cls,
        lily_var *method_var)
{
    /* Prevent class methods from being accessed globally, because they're now
       longer globals. */
    if (method_var == symtab->active_module->var_chain)
        symtab->active_module->var_chain = method_var->next;

    method_var->next = (lily_var *)cls->members;
    cls->members = (lily_named_sym *)method_var;
}

static lily_module_entry *find_module(lily_module_entry *module,
        const char *name)
{
    lily_module_link *link_iter = module->module_chain;
    lily_module_entry *result = NULL;
    while (link_iter) {
        char *as_name = link_iter->as_name;
        char *loadname = link_iter->module->loadname;

        /* If it was imported like 'import x as y', then as_name will be
           non-null. In such a case, don't allow fallback access as 'x', just
           in case something else is imported with the name 'x'. */
        if ((as_name && strcmp(as_name, name) == 0) ||
            (as_name == NULL && strcmp(loadname, name) == 0)) {
            result = link_iter->module;
            break;
        }

        link_iter = link_iter->next_module;
    }

    return result;
}

/* Create a new property and add it into the class. As a convenience, the
   newly-made property is also returned. */
lily_prop_entry *lily_add_class_property(lily_symtab *symtab, lily_class *cls,
        lily_type *type, const char *name, int flags)
{
    lily_prop_entry *entry = lily_malloc(sizeof(lily_prop_entry));
    char *entry_name = lily_malloc(strlen(name) + 1);

    strcpy(entry_name, name);

    entry->item_kind = ITEM_TYPE_PROPERTY;
    entry->flags = flags;
    entry->name = entry_name;
    entry->type = type;
    entry->name_shorthash = shorthash_for_name(entry_name);
    entry->next = NULL;
    entry->id = cls->prop_count;
    entry->cls = cls;
    cls->prop_count++;

    entry->next = (lily_prop_entry *)cls->members;
    cls->members = (lily_named_sym *)entry;

    return entry;
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
lily_variant_class *lily_new_variant(lily_symtab *symtab, lily_class *enum_cls,
        const char *name, int variant_id)
{
    lily_variant_class *variant = lily_malloc(sizeof(lily_variant_class));

    variant->item_kind = ITEM_TYPE_VARIANT;
    variant->flags = CLS_IS_VARIANT | CLS_EMPTY_VARIANT;
    variant->variant_id = variant_id;
    variant->parent = enum_cls;
    variant->build_type = NULL;
    variant->shorthash = shorthash_for_name(name);
    variant->name = lily_malloc(strlen(name) + 1);
    strcpy(variant->name, name);

    variant->next = symtab->active_module->class_chain;
    symtab->active_module->class_chain = (lily_class *)variant;

    /* Variant classes do not need a unique class id because they are not
       compared in ts. In vm, they're always accessed through their enum. */

    return variant;
}

/* Scoped variants are stored within the enum they're part of. This will try to
   find a variant stored within 'enum_cls'. */
lily_variant_class *lily_find_scoped_variant(lily_class *enum_cls,
        const char *name)
{
    int i;
    uint64_t shorthash = shorthash_for_name(name);
    lily_variant_class *ret = NULL;

    for (i = 0;i < enum_cls->variant_size;i++) {
        lily_variant_class *cls = enum_cls->variant_members[i];
        if (cls->shorthash == shorthash &&
            strcmp(cls->name, name) == 0) {
            ret = cls;
        }
    }

    return ret;
}

lily_literal *make_variant_default(lily_symtab *symtab,
        lily_variant_class *variant)
{
    lily_instance_val *iv = lily_new_instance_val();
    iv->instance_id = variant->parent->id;
    iv->variant_id = variant->variant_id;
    iv->num_values = 0;

    lily_literal *v = (lily_literal *)lily_new_empty_value();

    /* This value isn't interesting, but it might be swapped out with a value
       that is. */
    v->flags = VAL_IS_ENUM | VAL_IS_GC_SPECULATIVE;
    v->next_index = 0;
    v->reg_spot = lily_vs_pos(symtab->literals);
    v->value.instance = iv;
    /* Like with literals, fix the refcount so it's free'd later. */
    iv->refcount = 1;

    lily_vs_push(symtab->literals, (lily_value *)v);

    return v;
}

/* This is called when an enum class has finished scanning the variant members.
   If the enum is to be scoped, then the enums are bound within it. This is also
   where some callbacks are set on the enum (gc, eq, etc.) */
void lily_finish_enum(lily_symtab *symtab, lily_class *enum_cls, int is_scoped,
        lily_type *enum_type)
{
    int i, variant_count = 0;
    lily_class *class_iter = symtab->active_module->class_chain;
    while (class_iter != enum_cls) {
        variant_count++;
        class_iter = class_iter->next;
    }

    lily_variant_class **members =
            lily_malloc(variant_count * sizeof(lily_variant_class *));

    /* The ordering is important here. This makes it so the first variant will
       get the lowest id and be at 0. It makes indexing in vm sensible. */
    for (i = 0, class_iter = symtab->active_module->class_chain;
         i < variant_count;
         i++, class_iter = class_iter->next) {
        lily_variant_class *variant = (lily_variant_class *)class_iter;
        members[variant_count - 1 - i] = variant;

        if (variant->build_type == NULL) {
            variant->default_value = make_variant_default(symtab, variant);
            enum_cls->flags |= CLS_VALID_OPTARG;
        }
    }

    enum_cls->move_flags = VAL_IS_ENUM;
    enum_cls->variant_members = members;
    enum_cls->variant_size = variant_count;
    enum_cls->flags |= CLS_IS_ENUM;

    if (is_scoped) {
        enum_cls->flags |= CLS_ENUM_IS_SCOPED;
        /* This removes the variants from symtab's classes, so that parser has
           to get them from the enum. */
        symtab->active_module->class_chain = enum_cls;
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

/* This loads the symtab's classes into the vm's class table. That class table
   is used to give classes out to instances and enums that are built. The class
   information is later used to differentiate different instances. */
void lily_register_classes(lily_symtab *symtab, lily_vm_state *vm)
{
    lily_vm_ensure_class_table(vm, symtab->next_class_id + 1);

    lily_package *package_iter = symtab->first_package;
    while (package_iter) {
        lily_module_entry *module_iter = package_iter->first_module;
        while (module_iter) {
            lily_class *class_iter = module_iter->class_chain;
            while (class_iter) {
                if ((class_iter->flags & CLS_IS_VARIANT) == 0)
                    lily_vm_add_class_unchecked(vm, class_iter);

                class_iter = class_iter->next;
            }
            module_iter = module_iter->root_next;
        }
        package_iter = package_iter->root_next;
    }

    /* Variants have an id of 0 since they don't need to go into the class
       table. However, this causes them to take over Integer's slot. This makes
       sure that Integer has Integer's slot. */
    lily_vm_add_class_unchecked(vm, symtab->integer_class);
}

/* Try to find an module named 'name' within the given import. If the given
   import is NULL, then both the current import AND the builtin import are
   searched. */
lily_module_entry *lily_find_module(lily_symtab *symtab,
        lily_module_entry *module, const char *name)
{
    lily_module_entry *result;
    if (module == NULL)
        result = find_module(symtab->active_module, name);
    else
        result = find_module(module, name);

    return result;
}

/* This checks to see if a module has already been loaded. The path provided
   includes dirs, and is relative to the root module of the package. However,
   the path does not include the suffix so that this works regardless of what
   kind of thing was loaded (library or real file). */
lily_module_entry *lily_find_module_by_path(lily_package *package,
        const char *path)
{
    int cmp_len = strlen(path);
    lily_module_entry *module_iter = package->first_module;

    while (module_iter) {
        if (module_iter->cmp_len == cmp_len &&
            strncmp(module_iter->path, path, cmp_len) == 0)
            break;

        module_iter = module_iter->root_next;
    }

    return module_iter;
}

/* Check if a package named 'name' exists within 'module'. */
lily_package *lily_find_package(lily_module_entry *module, const char *name)
{
    lily_package_link *link_iter = module->parent->linked_packages;
    lily_package *result = NULL;

    while (link_iter) {
        if (strcmp(link_iter->package->name, name) == 0) {
            result = link_iter->package;
            break;
        }

        link_iter = link_iter->next;
    }

    return result;
}

/* This...is called to 'fix' how many generics are available in the current
   class. As a 'neat' side-effect, it also sets how many generics that
   'decl_class' has. */
void lily_update_symtab_generics(lily_symtab *symtab, int count)
{
    /* The symtab special cases all types holding generic information so
       that they're unique, together, and in numerical order. */
    lily_type *type_iter = symtab->generic_class->all_subtypes;
    int i = 1;

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
}
