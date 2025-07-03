#include <ctype.h>
#include <string.h>

#include "lily.h"
#include "lily_alloc.h"
#include "lily_symtab.h"
#include "lily_value.h"
#include "lily_vm.h"

/* If a String/ByteString is longer than this, don't do duplicate lookups.
   It's probably unique and dup checking is a waste of time. */
#define MAX_STRING_CACHE_LENGTH 32

/***
 *      ____       _
 *     / ___|  ___| |_ _   _ _ __
 *     \___ \ / _ \ __| | | | '_ \
 *      ___) |  __/ |_| |_| | |_) |
 *     |____/ \___|\__|\__,_| .__/
 *                          |_|
 */

static lily_value_stack *new_value_stack(uint16_t);

lily_symtab *lily_new_symtab(void)
{
    lily_symtab *symtab = lily_malloc(sizeof(*symtab));

    symtab->hidden_class_chain = NULL;
    symtab->hidden_function_chain = NULL;
    symtab->literals = new_value_stack(4);
    symtab->next_class_id = 1;
    symtab->next_global_id = 0;

    return symtab;
}

void lily_set_prelude(lily_symtab *symtab, lily_module_entry *prelude)
{
    symtab->prelude_module = prelude;
    symtab->active_module = prelude;
}

static void free_boxed_syms_since(lily_boxed_sym *sym, lily_boxed_sym *stop)
{
    lily_boxed_sym *sym_next;

    while (sym != stop) {
        sym_next = sym->next;

        lily_free(sym);

        sym = sym_next;
    }
}

#define free_boxed_syms(s) free_boxed_syms_since(s, NULL)

static void free_vars_since(lily_var *var, lily_var *stop)
{
    lily_var *var_next;

    while (var != stop) {
        var_next = var->next;

        lily_free(var->name);
        lily_free(var);

        var = var_next;
    }
}

#define free_vars(v) free_vars_since(v, NULL)

void lily_free_properties(lily_class *cls)
{
    lily_named_sym *prop_iter = cls->members;
    lily_named_sym *next_prop;

    while (prop_iter) {
        next_prop = prop_iter->next;

        if (prop_iter->item_kind & ITEM_IS_VARIANT) {
            lily_variant_class *variant = (lily_variant_class *)prop_iter;

            if (variant->keywords) {
                lily_free(variant->keywords[0]);
                lily_free(variant->keywords);
            }
        }

        lily_free(prop_iter->name);
        lily_free(prop_iter);

        prop_iter = next_prop;
    }
}

static void free_classes_until(lily_class *class_iter, lily_class *stop)
{
    while (class_iter != stop) {
        lily_free(class_iter->name);

        if (class_iter->members != NULL)
            lily_free_properties(class_iter);

        lily_type *type_iter = class_iter->all_subtypes;
        lily_type *type_next;
        while (type_iter) {
            type_next = type_iter->next;
            lily_free(type_iter->subtypes);
            lily_free(type_iter);
            type_iter = type_next;
        }

        lily_class *class_next = class_iter->next;
        lily_free(class_iter);
        class_iter = class_next;
    }
}

#define free_classes(iter) free_classes_until(iter, NULL)

static void hide_classes(lily_symtab *symtab, lily_class *class_iter,
        lily_class *stop)
{
    lily_class *hidden_top = symtab->hidden_class_chain;

    while (class_iter != stop) {
        lily_class *class_next = class_iter->next;

        class_iter->next = hidden_top;
        hidden_top = class_iter;

        class_iter = class_next;
    }

    symtab->hidden_class_chain = hidden_top;
}

static void free_literals(lily_value_stack *literals)
{
    lily_value **data = literals->data;
    uint16_t i;

    for (i = 0;i < literals->pos;i++) {
        lily_literal *lit = (lily_literal *)data[i];

        /* These literals stay alive by having one ref since they live in
           symtab's literal space. They can go away now. */
        if (lit->flags &
            (V_BYTESTRING_FLAG | V_STRING_FLAG | V_FUNCTION_FLAG)) {
            lit->flags |= VAL_IS_DEREFABLE;
            lily_deref((lily_value *)lit);
        }

        lily_free(lit);
    }

    lily_free(literals->data);
    lily_free(literals);
}

void lily_free_module_symbols(lily_symtab *symtab, lily_module_entry *entry)
{
    (void) symtab;
    free_classes(entry->class_chain);
    free_vars(entry->var_chain);
    if (entry->boxed_chain)
        free_boxed_syms(entry->boxed_chain);
}

void lily_rewind_symtab(lily_symtab *symtab, lily_module_entry *main_module,
        lily_class *stop_class, lily_var *stop_var, lily_boxed_sym *stop_box,
        int executing)
{
    symtab->active_module = main_module;

    if (main_module->boxed_chain != stop_box) {
        free_boxed_syms_since(main_module->boxed_chain, stop_box);
        main_module->boxed_chain = stop_box;
    }

    if (main_module->var_chain != stop_var) {
        free_vars_since(main_module->var_chain, stop_var);
        main_module->var_chain = stop_var;
    }

    if (main_module->class_chain != stop_class) {
        if (executing)
            hide_classes(symtab, main_module->class_chain, stop_class);
        else
            free_classes_until(main_module->class_chain, stop_class);

        main_module->class_chain = stop_class;
    }
}

void lily_free_symtab(lily_symtab *symtab)
{
    free_literals(symtab->literals);

    free_classes(symtab->hidden_class_chain);
    free_vars(symtab->hidden_function_chain);

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

lily_value_stack *new_value_stack(uint16_t initial)
{
    lily_value_stack *result = lily_malloc(sizeof(*result));

    result->data = lily_malloc(initial * sizeof(*result->data));
    result->pos = 0;
    result->size = initial;

    return result;
}

static void push_literal(lily_symtab *symtab, lily_value *value)
{
    lily_value_stack *literals = symtab->literals;

    if (literals->pos + 1 > literals->size) {
        literals->size *= 2;
        literals->data = lily_realloc(literals->data,
                literals->size * sizeof(*literals->data));
    }

    literals->data[literals->pos] = value;
    literals->pos++;
}

lily_value *lily_literal_at(lily_symtab *symtab, uint16_t index)
{
    return symtab->literals->data[index];
}

static lily_value *new_value_of_bytestring(lily_bytestring_val *bv)
{
    lily_value *v = lily_malloc(sizeof(*v));

    v->flags = V_BYTESTRING_FLAG | V_BYTESTRING_BASE | VAL_IS_DEREFABLE;
    v->value.string = (lily_string_val *)bv;
    return v;
}

static lily_value *new_value_of_double(double d)
{
    lily_value *v = lily_malloc(sizeof(*v));

    v->flags = V_DOUBLE_FLAG | V_DOUBLE_BASE;
    v->value.doubleval = d;
    return v;
}

static lily_value *new_value_of_integer(int64_t i)
{
    lily_value *v = lily_malloc(sizeof(*v));

    v->flags = V_INTEGER_FLAG | V_INTEGER_BASE;
    v->value.integer = i;
    return v;
}

static lily_value *new_value_of_string(lily_string_val *sv)
{
    lily_value *v = lily_malloc(sizeof(*v));

    v->flags = V_STRING_FLAG | V_STRING_BASE | VAL_IS_DEREFABLE;
    v->value.string = sv;
    return v;
}

static lily_value *new_value_of_unit(void)
{
    lily_value *v = lily_malloc(sizeof(*v));

    v->flags = V_UNIT_BASE;
    v->value.integer = 0;
    return v;
}

/* Literals take advantage of lily_value having extra padding in it. That extra
   padding will have the index of the next literal of that kind. The only
   trouble is finding the first one with the given flag to start with. */
static lily_literal *first_lit_of(lily_value_stack *literals, uint32_t to_find)
{
    lily_value **data = literals->data;
    uint16_t i;

    for (i = 0;i < literals->pos;i++) {
        lily_literal *lit = (lily_literal *)data[i];
        if (FLAGS_TO_BASE(lit) == to_find)
            return lit;
    }

    return NULL;
}

lily_literal *lily_get_integer_literal(lily_symtab *symtab,
        lily_type **type_ret, int64_t int_val)
{
    *type_ret = (lily_type *)symtab->integer_class;

    lily_literal *iter = first_lit_of(symtab->literals, V_INTEGER_BASE);

    while (iter) {
        if (iter->value.integer == int_val)
            return iter;

        uint16_t next = iter->next_index;

        if (next == 0)
            break;
        else
            iter = (lily_literal *)lily_literal_at(symtab, next);
    }

    if (iter)
        iter->next_index = symtab->literals->pos;

    lily_literal *v = (lily_literal *)new_value_of_integer(int_val);
    v->reg_spot = symtab->literals->pos;
    v->next_index = 0;

    push_literal(symtab, (lily_value *)v);
    return (lily_literal *)v;
}

lily_literal *lily_get_double_literal(lily_symtab *symtab, lily_type **type_ret,
        double dbl_val)
{
    *type_ret = (lily_type *)symtab->double_class;

    lily_literal *iter = first_lit_of(symtab->literals, V_DOUBLE_BASE);

    while (iter) {
        if (iter->value.doubleval == dbl_val)
            return iter;

        uint16_t next = iter->next_index;

        if (next == 0)
            break;
        else
            iter = (lily_literal *)lily_literal_at(symtab, next);
    }

    if (iter)
        iter->next_index = symtab->literals->pos;

    lily_literal *v = (lily_literal *)new_value_of_double(dbl_val);
    v->reg_spot = symtab->literals->pos;
    v->next_index = 0;

    push_literal(symtab, (lily_value *)v);
    return (lily_literal *)v;
}

lily_literal *lily_get_bytestring_literal(lily_symtab *symtab,
        lily_type **type_ret, const char *want_string, uint32_t len)
{
    *type_ret = (lily_type *)symtab->bytestring_class;

    lily_literal *iter = first_lit_of(symtab->literals, V_BYTESTRING_BASE);

    if (len < MAX_STRING_CACHE_LENGTH) {
        while (iter) {
            if (iter->value.string->size == len &&
                memcmp(iter->value.string->string, want_string, len) == 0)
                return iter;

            uint16_t next = iter->next_index;

            if (next == 0)
                break;
            else
                iter = (lily_literal *)lily_literal_at(symtab, next);
        }
    }
    else {
        while (iter) {
            uint16_t next = iter->next_index;

            if (next == 0)
                break;
            else
                iter = (lily_literal *)lily_literal_at(symtab, next);
        }
    }

    if (iter)
        iter->next_index = symtab->literals->pos;

    lily_bytestring_val *sv = lily_new_bytestring_raw(want_string, len);
    lily_literal *v = (lily_literal *)new_value_of_bytestring(sv);

    /* Drop the derefable marker. */
    v->flags = V_BYTESTRING_FLAG | V_BYTESTRING_BASE;
    v->reg_spot = symtab->literals->pos;
    v->next_index = 0;

    push_literal(symtab, (lily_value *)v);
    return (lily_literal *)v;
}

lily_literal *lily_get_string_literal(lily_symtab *symtab, lily_type **type_ret,
        const char *want_string)
{
    *type_ret = (lily_type *)symtab->string_class;

    lily_literal *iter = first_lit_of(symtab->literals, V_STRING_BASE);
    size_t want_string_len = strlen(want_string);

    if (want_string_len < MAX_STRING_CACHE_LENGTH) {
        while (iter) {
            if (iter->value.string->size == want_string_len &&
                strcmp(iter->value.string->string, want_string) == 0)
                return iter;

            uint16_t next = iter->next_index;

            if (next == 0)
                break;
            else
                iter = (lily_literal *)lily_literal_at(symtab, next);
        }
    }
    else {
        while (iter) {
            uint16_t next = iter->next_index;

            if (next == 0)
                break;
            else
                iter = (lily_literal *)lily_literal_at(symtab, next);
        }
    }

    if (iter)
        iter->next_index = symtab->literals->pos;

    lily_string_val *sv = lily_new_string_raw(want_string);
    lily_literal *v = (lily_literal *)new_value_of_string(sv);

    /* Drop the derefable marker. */
    v->flags = V_STRING_FLAG | V_STRING_BASE;
    v->reg_spot = symtab->literals->pos;
    v->next_index = 0;

    push_literal(symtab, (lily_value *)v);
    return (lily_literal *)v;
}

lily_literal *lily_get_unit_literal(lily_symtab *symtab)
{
    lily_literal *lit = first_lit_of(symtab->literals, V_UNIT_BASE);

    if (lit == NULL) {
        lily_literal *v = (lily_literal *)new_value_of_unit();
        v->reg_spot = symtab->literals->pos;
        v->next_index = 0;

        lit = v;
        push_literal(symtab, (lily_value *)v);
    }

    return lit;
}

void lily_new_function_literal(lily_symtab *symtab, lily_var *var,
        lily_value *v)
{
    var->reg_spot = symtab->literals->pos;
    push_literal(symtab, v);
}

/***
 *      ____                      _
 *     / ___|  ___  __ _ _ __ ___| |__
 *     \___ \ / _ \/ _` | '__/ __| '_ \
 *      ___) |  __/ (_| | | | (__| | | |
 *     |____/ \___|\__,_|_|  \___|_| |_|
 *
 */

/** Symtab also provides an interface for other parts of the interpreter to
    locate symbols. The symtab does not implement access restriction or
    implement dynaloading. Both of those are the responsibility of parser. */

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

static lily_sym *find_boxed_sym(lily_module_entry *m, const char *name,
        uint64_t shorthash)
{
    lily_boxed_sym *boxed_iter = m->boxed_chain;
    lily_sym *result = NULL;

    while (boxed_iter) {
        lily_named_sym *sym = boxed_iter->inner_sym;

        if (sym->shorthash == shorthash &&
            strcmp(sym->name, name) == 0) {
            result = (lily_sym *)sym;
            break;
        }

        boxed_iter = boxed_iter->next;
    }

    return result;
}

lily_class *lily_find_class(lily_module_entry *m, const char *name)
{
    uint64_t shorthash = shorthash_for_name(name);
    lily_class *result = NULL;
    lily_class *class_iter = m->class_chain;

    while (class_iter) {
        if (class_iter->shorthash == shorthash &&
            strcmp(class_iter->name, name) == 0) {
            result = class_iter;
            break;
        }

        if (class_iter->item_kind == ITEM_ENUM_FLAT) {
            lily_named_sym *sym_iter = class_iter->members;

            while (sym_iter) {
                if (sym_iter->shorthash == shorthash &&
                    strcmp(sym_iter->name, name) == 0 &&
                    sym_iter->item_kind & ITEM_IS_VARIANT) {
                    result = (lily_class *)sym_iter;
                    break;
                }

                sym_iter = sym_iter->next;
            }

            if (result)
                break;
        }

        class_iter = class_iter->next;
    }

    if (result == NULL && m->boxed_chain) {
        lily_sym *sym = find_boxed_sym(m, name, shorthash);

        if (sym &&
            sym->item_kind & (ITEM_IS_CLASS | ITEM_IS_ENUM | ITEM_IS_VARIANT))
            result = (lily_class *)sym;
    }

    return result;
}

lily_var *lily_find_var(lily_module_entry *m, const char *name)
{
    uint64_t shorthash = shorthash_for_name(name);
    lily_var *result = NULL;
    lily_var *var_iter = m->var_chain;

    while (var_iter != NULL) {
        if (var_iter->shorthash == shorthash &&
            strcmp(var_iter->name, name) == 0) {
            result = var_iter;
            break;
        }
        var_iter = var_iter->next;
    }

    if (result == NULL && m->boxed_chain) {
        lily_sym *sym = find_boxed_sym(m, name, shorthash);

        if (sym && sym->item_kind & ITEM_IS_VARLIKE)
            result = (lily_var *)sym;
    }

    return result;
}

/* Look for 'name' as a member in 'cls' or any parent of 'cls'. This does not
   implement blocking against protected or private members. */
lily_named_sym *lily_find_member(lily_class *cls, const char *name)
{
    lily_named_sym *sym_iter = cls->members;
    lily_named_sym *result = NULL;

    while (1) {
        if (sym_iter != NULL) {
            uint64_t shorthash = shorthash_for_name(name);

            while (sym_iter) {
                if (sym_iter->shorthash == shorthash &&
                    strcmp(sym_iter->name, name) == 0) {
                    result = sym_iter;
                    break;
                }

                sym_iter = sym_iter->next;
            }
        }

        cls = cls->parent;

        if (result || cls == NULL)
            break;

        sym_iter = cls->members;
    }

    return result;
}

/* Look for 'name' as a member strictly in 'cls'. This does not implement
   blocking against protected or private members. */
lily_named_sym *lily_find_member_in_class(lily_class *cls, const char *name)
{
    lily_named_sym *sym_iter = cls->members;
    lily_named_sym *result = NULL;

    if (sym_iter != NULL) {
        uint64_t shorthash = shorthash_for_name(name);

        while (sym_iter) {
            if (sym_iter->shorthash == shorthash &&
                strcmp(sym_iter->name, name) == 0) {
                result = sym_iter;
                break;
            }

            sym_iter = sym_iter->next;
        }
    }

    return result;
}

/* Scoped variants are stored within the enum they're part of. This will try to
   find a variant stored within 'enum_cls'. */
lily_variant_class *lily_find_variant(lily_class *enum_cls, const char *name)
{
    uint64_t shorthash = shorthash_for_name(name);
    lily_named_sym *sym_iter = enum_cls->members;

    while (sym_iter) {
        if (sym_iter->shorthash == shorthash &&
            strcmp(sym_iter->name, name) == 0 &&
            sym_iter->item_kind & ITEM_IS_VARIANT) {
            break;
        }

        sym_iter = sym_iter->next;
    }

    return (lily_variant_class *)sym_iter;
}

lily_module_entry *lily_find_module(lily_module_entry *module, const char *name)
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

        link_iter = link_iter->next;
    }

    return result;
}

lily_module_entry *lily_find_module_by_path(lily_symtab *symtab,
        const char *path)
{
    /* Modules are linked starting after prelude. Skip that, it's not what's
       being looked for. */
    lily_module_entry *module_iter = symtab->prelude_module->next;
    size_t len = strlen(path);

    while (module_iter) {
        if (module_iter->cmp_len == len &&
            strcmp(module_iter->path, path) == 0) {
            break;
        }

        module_iter = module_iter->next;
    }

    return module_iter;
}

lily_module_entry *lily_find_registered_module(lily_symtab *symtab,
        const char *name)
{
    /* Start after the prelude module because nothing actually wants the prelude
       module. */
    lily_module_entry *module_iter = symtab->prelude_module->next;

    while (module_iter) {
        if (module_iter->flags & MODULE_IS_REGISTERED &&
            strcmp(module_iter->loadname, name) == 0)
            break;

        module_iter = module_iter->next;
    }

    return module_iter;
}

/***
 *       ____ _                  _______
 *      / ___| | __ _ ___ ___   / / ____|_ __  _   _ _ __ ___
 *     | |   | |/ _` / __/ __| / /|  _| | '_ \| | | | '_ ` _ \
 *     | |___| | (_| \__ \__ \/ / | |___| | | | |_| | | | | | |
 *      \____|_|\__,_|___/___/_/  |_____|_| |_|\__,_|_| |_| |_|
 *
 */

/* This creates a new class that is returned to the caller. The newly-made class
   is not added to the symtab, and has no id set upon it. */
lily_class *lily_new_raw_class(const char *name, uint16_t line_num)
{
    lily_class *new_class = lily_malloc(sizeof(*new_class));
    char *name_copy = lily_malloc((strlen(name) + 1) * sizeof(*name_copy));

    strcpy(name_copy, name);

    new_class->item_kind = ITEM_CLASS_NATIVE;
    new_class->flags = 0;

    /* New classes start off as having 0 generics, as well as being their own
       type. User-defined classes should fix the self type if they fix the
       generic count. */
    new_class->self_type = (lily_type *)new_class;
    new_class->type_subtype_count = 0;

    new_class->parent = NULL;
    new_class->shorthash = shorthash_for_name(name);
    new_class->line_num = line_num;
    new_class->doc_id = UINT16_MAX;
    new_class->name = name_copy;
    new_class->generic_count = 0;
    new_class->prop_count = 0;
    new_class->members = NULL;
    new_class->module = NULL;
    new_class->all_subtypes = NULL;
    new_class->dyna_start = 0;
    new_class->inherit_depth = 0;

    return new_class;
}

/* This creates a new generic class (A, B, C, etc.) for the generic pool. These
   are created here because they need to look like a real class, and the real
   class init is just above this one. */
lily_generic_class *lily_new_generic_class(const char *name)
{
    lily_generic_class *new_class = lily_malloc(sizeof(*new_class));
    char *name_copy = lily_malloc((strlen(name) + 1) * sizeof(*name_copy));

    strcpy(name_copy, name);

    new_class->item_kind = ITEM_CLASS_NATIVE;
    new_class->flags = TYPE_IS_UNRESOLVED | CLS_GC_SPECULATIVE;
    new_class->generic_pos = 0;
    new_class->self_type = (lily_type *)new_class;
    new_class->type_subtype_count = 0;
    new_class->parent = NULL;
    new_class->shorthash = shorthash_for_name(name);
    new_class->line_num = 0;
    new_class->doc_id = UINT16_MAX;
    new_class->name = name_copy;
    new_class->generic_count_not_pos = 0;
    new_class->prop_count = 0;
    new_class->members = NULL;
    new_class->dyna_start = 0;
    new_class->inherit_depth = 0;

    return new_class;
}

/* This creates a new class-like entity (class or enum) that's linked into the
   current module's class-like listing. The new class-like entry is given the
   next available id. */
lily_class *lily_new_class(lily_symtab *symtab, const char *name,
        uint16_t line_num)
{
    lily_class *new_class = lily_new_raw_class(name, line_num);

    new_class->module = symtab->active_module;
    new_class->line_num = line_num;

    new_class->id = symtab->next_class_id;
    symtab->next_class_id++;

    new_class->next = symtab->active_module->class_chain;
    symtab->active_module->class_chain = new_class;

    return new_class;
}

/* Use this to create a new class that represents an enum. */
lily_class *lily_new_enum_class(lily_symtab *symtab, const char *name,
        uint16_t line_num)
{
    lily_class *new_class = lily_new_class(symtab, name, line_num);

    symtab->next_class_id--;
    new_class->item_kind = ITEM_ENUM_FLAT;
    new_class->id = 0;

    /* Enums don't interact with the type system or emitter until their
       definitions, which come after the variants. Hold off on giving them an id
       until the variants are done. By doing that, enums and their variants can
       be given a sequential id. That order is expected by foreign bindings for
       variants, which use 'enum id + variant pos' to compute variant id. */
    return new_class;
}

/* Create a new property and add it into the class. As a convenience, the
   newly-made property is also returned. */
lily_prop_entry *lily_add_class_property(lily_class *cls, lily_type *type,
        const char *name, uint16_t line_num, uint16_t flags)
{
    lily_prop_entry *entry = lily_malloc(sizeof(*entry));
    char *entry_name = lily_malloc((strlen(name) + 1) * sizeof(*entry_name));

    strcpy(entry_name, name);

    entry->item_kind = ITEM_PROPERTY;
    entry->flags = flags;
    entry->name = entry_name;
    entry->type = type;
    entry->shorthash = shorthash_for_name(entry_name);
    entry->id = cls->prop_count;
    entry->line_num = line_num;
    entry->doc_id = UINT16_MAX;
    entry->parent = cls;
    cls->prop_count++;

    entry->next = cls->members;
    cls->members = (lily_named_sym *)entry;

    return entry;
}

/* This creates a new variant called 'name' and installs it into 'enum_cls'. */
lily_variant_class *lily_new_variant_class(lily_class *enum_cls,
        const char *name, uint16_t line_num)
{
    lily_variant_class *variant = lily_malloc(sizeof(*variant));

    variant->item_kind = ITEM_VARIANT_EMPTY;
    variant->flags = 0;

    /* Variants get an id when they're done collecting. */
    variant->cls_id = 0;
    variant->parent = enum_cls;
    variant->type_subtype_count = 0;
    variant->build_type = NULL;
    variant->shorthash = shorthash_for_name(name);
    variant->line_num = line_num;
    variant->keywords = NULL;
    variant->name = lily_malloc((strlen(name) + 1) * sizeof(*variant->name));
    strcpy(variant->name, name);

    variant->next = enum_cls->members;
    enum_cls->members = (lily_named_sym *)variant;
    enum_cls->variant_size++;

    return variant;
}

/* This is called after all variants of an enum are collected, but before
   methods are parsed. This function transforms the reverse ids that have been
   held so far into proper symtab ids.

   Handing out enum and variant ids this way ensures that variant ids always
   occur exactly after their enum parent ids. */
void lily_fix_enum_variant_ids(lily_symtab *symtab, lily_class *enum_cls)
{
    enum_cls->id = symtab->next_class_id;
    symtab->next_class_id += enum_cls->variant_size;

    uint16_t next_id = symtab->next_class_id;
    lily_named_sym *member_iter = enum_cls->members;

    /* Method collection hasn't happened yet, so all members are variants. */
    while (member_iter) {
        lily_variant_class *variant = (lily_variant_class *)member_iter;

        variant->cls_id = next_id;
        next_id--;

        member_iter = member_iter->next;
    }

    symtab->next_class_id++;
}

/* Types have a cached class id (type->cls_id) that needs to be updated, if the
   enum takes inner types. */
void lily_fix_enum_type_ids(lily_class *enum_cls)
{
    /* Monomorphic enums are their own self type, nothing to do. */
    if (enum_cls->generic_count == 0)
        return;

    lily_type *iter = enum_cls->all_subtypes;

    while (iter) {
        iter->cls_id = enum_cls->id;
        iter = iter->next;
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

    lily_module_entry *module_iter = symtab->prelude_module;
    while (module_iter) {
        lily_class *class_iter = module_iter->class_chain;
        while (class_iter) {
            lily_vm_add_class_unchecked(vm, class_iter);

            if (class_iter->item_kind & ITEM_IS_ENUM) {
                lily_named_sym *sym_iter = class_iter->members;
                while (sym_iter) {
                    if (sym_iter->item_kind & ITEM_IS_VARIANT) {
                        lily_class *v = (lily_class *)sym_iter;
                        lily_vm_add_class_unchecked(vm, v);
                    }

                    sym_iter = sym_iter->next;
                }
            }
            class_iter = class_iter->next;
        }
        module_iter = module_iter->next;
    }

    /* Variants have an id of 0 since they don't need to go into the class
       table. However, this causes them to take over Integer's slot. This makes
       sure that Integer has Integer's slot. */
    lily_vm_add_class_unchecked(vm, symtab->integer_class);
}

void lily_add_symbol_ref(lily_module_entry *m, lily_sym *sym)
{
    lily_boxed_sym *box = lily_malloc(sizeof(*box));

    /* lily_boxed_sym is kept internal to symtab, so it doesn't need an id or
       an item kind set. */
    box->inner_sym = (lily_named_sym *)sym;
    box->next = m->boxed_chain;
    m->boxed_chain = box;
}
