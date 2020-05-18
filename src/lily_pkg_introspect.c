/**
library introspect

This package provides introspection into the interpreter. Introspection allows
finding out what modules have been imported and what symbols that those modules
hold. Note that the api that this package exports is currently limited.

One caveat with introspection is that the interpreter does not automatically
load all symbols from foreign libraries. This mechanism (dynaload) means that
introspecting a foreign library may not include all of the symbols inside of
it. Introspection does not provide any means of finding symbols that are not
loaded.
*/

#include "lily_vm.h"
#include "lily_parser.h"
#include "lily_core_types.h"
#include "lily_symtab.h"

#include "lily.h"
#include "lily_pkg_introspect_bindings.h"

/* Fetch an internal field of a struct, pushing it and returning it as the
   result. */
#define FETCH_FIELD(source_type, inner_type, \
                    target_type, target_field, \
                    push_fn) \
lily_introspect_##source_type *introspect_entry = ARG_##source_type(s, 0); \
inner_type *c = introspect_entry->entry; \
target_type target = c->target_field; \
 \
push_fn(s, target); \
lily_return_top(s);

/* FETCH_FIELD, but with a fallback value in case the field is NULL. */
#define FETCH_FIELD_SAFE(source_type, inner_type, \
                         target_type, target_field, \
                         push_fn, fallback) \
lily_introspect_##source_type *introspect_entry = ARG_##source_type(s, 0); \
inner_type *c = introspect_entry->entry; \
target_type target = c->target_field; \
 \
if (target == NULL) \
    target = fallback; \
 \
push_fn(s, target); \
lily_return_top(s);

#define UNPACK_FIRST_ARG(boxed_type, raw_type) \
lily_introspect_##boxed_type *introspect_entry = ARG_##boxed_type(s, 0); \
raw_type entry = introspect_entry->entry

static void destroy_ClassEntry(lily_introspect_ClassEntry *c)
{
}

static void destroy_EnumEntry(lily_introspect_EnumEntry *c)
{
}

static void destroy_FunctionEntry(lily_introspect_FunctionEntry *f)
{
}

static void destroy_MethodEntry(lily_introspect_MethodEntry *m)
{
}

static void destroy_ModuleEntry(lily_introspect_ModuleEntry *m)
{
}

static void destroy_PropertyEntry(lily_introspect_PropertyEntry *p)
{
}

static void destroy_VarEntry(lily_introspect_VarEntry *v)
{
}

static void destroy_VariantEntry(lily_introspect_VariantEntry *v)
{
}

static void destroy_TypeEntry(lily_introspect_TypeEntry *t)
{
}

/* Lily's symbols all start with a next field and are straightforward to extract
   into one of the boxed fields mentioned above. */
#define BUILD_LIST_FROM(check_func, build_func) \
int i, count = 0; \
 \
while (source_iter) { \
    if (check_func(source_iter)) \
        count++; \
 \
    source_iter = source_iter->next; \
} \
 \
lily_container_val *lv = lily_push_list(s, count); \
 \
for (i = 0;source != NULL;source = source->next) { \
    if (check_func(source)) { \
        build_func(s, source); \
        lily_con_set_from_stack(s, lv, i); \
        i++; \
    } \
} \
 \
lily_return_top(s);

/* This is for properties and methods which take the class as well as a source
   value to wrap over. */
#define BUILD_LIST_FROM_2(check_func, build_func) \
int i, count = 0; \
 \
while (source_iter) { \
    if (check_func(source_iter)) \
        count++; \
 \
    source_iter = source_iter->next; \
} \
 \
lily_container_val *lv = lily_push_list(s, count); \
 \
for (i = 0;source != NULL;source = source->next) { \
    if (check_func(source)) { \
        build_func(s, entry, source); \
        lily_con_set_from_stack(s, lv, i); \
        i++; \
    } \
} \
 \
lily_return_top(s);

/* Flat variants are listed at the toplevel of whatever module they're a part of
   instead of in their enum. Their check function needs to take the module and
   the enum to do a parent check. */
#define BUILD_FLAT_VARIANT_LIST(check_func, build_func) \
int i, count = 0; \
 \
while (source_iter) { \
    if (check_func(source_iter, parent)) \
        count++; \
 \
    source_iter = source_iter->next; \
} \
 \
lily_container_val *lv = lily_push_list(s, count); \
 \
for (i = 0;source != NULL;source = source->next) { \
    if (check_func(source, parent)) { \
        build_func(s, entry, source); \
        lily_con_set_from_stack(s, lv, i); \
        i++; \
    } \
} \
 \
lily_return_top(s);

static int allow_all(void *any)
{
    return 1;
}

static int allow_boxed_classes(lily_boxed_sym *sym)
{
    return sym->inner_sym->item_kind & ITEM_IS_CLASS;
}

static int allow_boxed_enums(lily_boxed_sym *sym)
{
    return sym->inner_sym->item_kind & ITEM_IS_ENUM;
}

static int allow_boxed_functions(lily_boxed_sym *sym)
{
    return sym->inner_sym->flags & VAR_IS_READONLY;
}

static int allow_boxed_variants(lily_boxed_sym *sym)
{
    return sym->inner_sym->item_kind & ITEM_IS_VARIANT;
}

static int allow_boxed_vars(lily_boxed_sym *sym)
{
    return sym->inner_sym->item_kind == ITEM_VAR;
}

static int allow_classes(lily_class *cls)
{
    return cls->item_kind & ITEM_IS_CLASS;
}

static int allow_enums(lily_class *cls)
{
    return cls->item_kind & ITEM_IS_ENUM;
}

static int allow_flat_variants(lily_named_sym *sym, lily_class *parent)
{
    return sym->item_kind & ITEM_IS_VARIANT &&
           ((lily_variant_class *)sym)->parent == parent;
}

static int allow_functions(lily_var *var)
{
    return var->flags & VAR_IS_READONLY;
}

static int allow_methods(lily_named_sym *sym)
{
    return sym->item_kind == ITEM_VAR;
}

static int allow_properties(lily_named_sym *sym)
{
    return sym->item_kind == ITEM_PROPERTY;
}

static int allow_scoped_variants(lily_named_sym *sym)
{
    return sym->item_kind & ITEM_IS_VARIANT;
}

static int allow_vars(lily_var *var)
{
    return (var->flags & VAR_IS_READONLY) == 0;
}

static void make_class(lily_state *s, lily_class *source)
{
    lily_introspect_ClassEntry *new_entry = INIT_ClassEntry(s);
    new_entry->entry = source;
}

static void make_enum(lily_state *s, lily_class *source)
{
    lily_introspect_EnumEntry *new_entry = INIT_EnumEntry(s);
    new_entry->entry = source;
}

static void make_function(lily_state *s, lily_var *source)
{
    lily_introspect_FunctionEntry *new_entry = INIT_FunctionEntry(s);
    new_entry->entry = source;
}

static void make_method(lily_state *s, lily_class *entry,
                        lily_named_sym *source)
{
    lily_introspect_MethodEntry *new_entry = INIT_MethodEntry(s);
    new_entry->entry = (lily_var *)source;
    new_entry->parent = entry;
}

static void make_module(lily_state *s, lily_module_entry *source) {
    lily_introspect_ModuleEntry *new_entry = INIT_ModuleEntry(s);
    new_entry->entry = source;
}

static void make_module_from_link(lily_state *s, lily_module_link *source) {
    lily_introspect_ModuleEntry *new_entry = INIT_ModuleEntry(s);
    new_entry->entry = source->module;
}

static void make_property(lily_state *s, lily_class *entry,
                          lily_named_sym *source)
{
    lily_introspect_PropertyEntry *new_entry = INIT_PropertyEntry(s);
    new_entry->entry = (lily_prop_entry *)source;
    new_entry->parent = entry;
}

static void make_var(lily_state *s, lily_var *source)
{
    lily_introspect_VarEntry *new_entry = INIT_VarEntry(s);
    new_entry->entry = source;
}

static void make_variant(lily_state *s, lily_class *entry,
                         lily_named_sym *source)
{
    lily_introspect_VariantEntry *new_entry = INIT_VariantEntry(s);
    new_entry->entry = (lily_variant_class *)source;
    new_entry->parent = entry;
}

static void boxed_make_class(lily_state *s, lily_boxed_sym *source)
{
    make_class(s, (lily_class *)source->inner_sym);
}

static void boxed_make_enum(lily_state *s, lily_boxed_sym *source)
{
    make_enum(s, (lily_class *)source->inner_sym);
}

static void boxed_make_function(lily_state *s, lily_boxed_sym *source)
{
    make_function(s, (lily_var *)source->inner_sym);
}

static void boxed_make_var(lily_state *s, lily_boxed_sym *source)
{
    make_var(s, (lily_var *)source->inner_sym);
}

static void boxed_make_variant(lily_state *s, lily_boxed_sym *source)
{
	lily_variant_class *v = (lily_variant_class *)source->inner_sym;

    make_variant(s, v->parent, (lily_named_sym *)v);
}

/* Unpack the first argument given and send the type it has. All foreign classes
   must have lily_sym * as the first field. Internally, the type is at a common
   location for properties, definitions, and vars. This library only needs to
   guarantee the former for this to work across symbols. */
static void unpack_and_return_type(lily_state *s)
{
    UNPACK_FIRST_ARG(FunctionEntry, lily_var *);
    lily_type *t = entry->type;

    lily_introspect_TypeEntry *boxed_type = INIT_TypeEntry(s);
    boxed_type->entry = t;

    lily_return_top(s);
}

static void return_module_id(lily_state *s, lily_module_entry *m)
{
    lily_return_integer(s, (int64_t)m);
}

static void return_doc(lily_state *s, uint16_t doc_id)
{
    const char *str = "";

    if (doc_id != (uint16_t)-1)
        str = s->gs->parser->doc->data[doc_id][0];

    lily_push_string(s, str);
    lily_return_top(s);
}

static char **get_doc_text(lily_state *s, uint16_t doc_id)
{
    char **text = NULL;

    if (doc_id != (uint16_t)-1)
        text = s->gs->parser->doc->data[doc_id];

    return text;
}

/**
foreign class TypeEntry {
    layout {
        lily_type *entry;
    }
}

This is a foreign class that wraps over a type.
*/

/**
define TypeEntry.as_string: String

Return a string that describes the type provided. This uses the same type
printing that the interpreter's error messages use.
*/
void lily_introspect_TypeEntry_as_string(lily_state *s)
{
    UNPACK_FIRST_ARG(TypeEntry, lily_type *);
    lily_msgbuf *msgbuf = lily_mb_flush(lily_msgbuf_get(s));

    lily_mb_add_fmt(msgbuf, "^T", entry);
    lily_push_string(s, lily_mb_raw(msgbuf));
    lily_return_top(s);
}

/**
define TypeEntry.class_name: String

Return the name of the class that this type wraps over.
*/
void lily_introspect_TypeEntry_class_name(lily_state *s)
{
    FETCH_FIELD(TypeEntry, lily_type, const char *, cls->name,
            lily_push_string);
}

/**
define TypeEntry.module_id: Integer

Return a unique id that is based on the module that the underlying type comes
from. This is equivalent to the id that 'ModuleEntry.id' returns for that same
module.
*/
void lily_introspect_TypeEntry_module_id(lily_state *s)
{
    UNPACK_FIRST_ARG(TypeEntry, lily_type *);
    return_module_id(s, entry->cls->module);
}

/**
define TypeEntry.class_id: Integer

Return the id of the class that this type wraps over.
*/
void lily_introspect_TypeEntry_class_id(lily_state *s)
{
    UNPACK_FIRST_ARG(TypeEntry, lily_type *);
    lily_return_integer(s, entry->cls->id);
}

/**
native class ParameterEntry(name: String, key: String, t: TypeEntry) {
    var @name: String,
    var @keyword: String,
    var @type: TypeEntry
}

This is a native class representing a definition parameter.
*/
void lily_introspect_ParameterEntry_new(lily_state *s)
{
    lily_container_val *con = lily_push_super(s, ID_ParameterEntry(s), 3);
    lily_value *name = lily_arg_value(s, 0);
    lily_value *key = lily_arg_value(s, 1);
    lily_value *t = lily_arg_value(s, 2);

    SET_ParameterEntry__name(con, name);
    SET_ParameterEntry__keyword(con, key);
    SET_ParameterEntry__type(con, t);
    lily_return_super(s);
}

/**
foreign class VarEntry {
    layout {
        lily_var *entry;
    }
}

This is a foreign class that wraps over a var.
*/

/**
define VarEntry.doc: String

Return the docblock of this var, or an empty string. Docblocks are only saved
when a var is parsed in manifest mode.
*/
void lily_introspect_VarEntry_doc(lily_state *s)
{
    UNPACK_FIRST_ARG(VarEntry, lily_var *);
    return_doc(s, entry->doc_id);
}

/**
define VarEntry.line_number: Integer

Return the line number this var was declared on.
*/
void lily_introspect_VarEntry_line_number(lily_state *s)
{
    lily_introspect_VarEntry *introspect_entry = ARG_VarEntry(s, 0);
    lily_var *entry = introspect_entry->entry;
    lily_return_integer(s, entry->line_num);
}

/**
define VarEntry.name: String

Return the name of the var provided.
*/
void lily_introspect_VarEntry_name(lily_state *s)
{
    FETCH_FIELD(VarEntry, lily_var, const char *, name, lily_push_string);
}

/**
define VarEntry.type: TypeEntry

Return the type of the var provided.
*/
void lily_introspect_VarEntry_type(lily_state *s)
{
    unpack_and_return_type(s);
}

/**
foreign class PropertyEntry {
    layout {
        lily_prop_entry *entry;
        lily_class *parent;
    }
}

This is a foreign class that wraps over a class property.
*/

/**
define PropertyEntry.doc: String

Return the docblock of this property, or an empty string. Docblocks are only
saved when a property is parsed in manifest mode.
*/
void lily_introspect_PropertyEntry_doc(lily_state *s)
{
    UNPACK_FIRST_ARG(PropertyEntry, lily_prop_entry *);
    return_doc(s, entry->doc_id);
}

/**
define PropertyEntry.is_private: Boolean

Return `true` if the property is private, `false` otherwise.
*/
void lily_introspect_PropertyEntry_is_private(lily_state *s)
{
    UNPACK_FIRST_ARG(PropertyEntry, lily_prop_entry *);
    lily_return_boolean(s, !!(entry->flags & SYM_SCOPE_PRIVATE));
}

/**
define PropertyEntry.is_protected: Boolean

Return `true` if the property is protected, `false` otherwise.
*/
void lily_introspect_PropertyEntry_is_protected(lily_state *s)
{
    UNPACK_FIRST_ARG(PropertyEntry, lily_prop_entry *);
    lily_return_boolean(s, !!(entry->flags & SYM_SCOPE_PROTECTED));
}

/**
define PropertyEntry.is_public: Boolean

Return `true` if the property is public, `false` otherwise.
*/
void lily_introspect_PropertyEntry_is_public(lily_state *s)
{
    UNPACK_FIRST_ARG(PropertyEntry, lily_prop_entry *);
    lily_return_boolean(s, !!(entry->flags & SYM_SCOPE_PUBLIC));
}

/**
define PropertyEntry.name: String

Return the name of the property.
*/
void lily_introspect_PropertyEntry_name(lily_state *s)
{
    FETCH_FIELD(PropertyEntry, lily_prop_entry, const char *, name, lily_push_string);
}

/**
define PropertyEntry.type: TypeEntry

Return the type of the property.
*/
void lily_introspect_PropertyEntry_type(lily_state *s)
{
    unpack_and_return_type(s);
}

/**
foreign class FunctionEntry {
    layout {
        lily_var *entry;
    }
}

This is a foreign class that wraps over a toplevel function of a package.
*/

/**
define FunctionEntry.doc: String

Return the docblock of this function, or an empty string. Docblocks are only
saved when a function is parsed in manifest mode.
*/
void lily_introspect_FunctionEntry_doc(lily_state *s)
{
    UNPACK_FIRST_ARG(FunctionEntry, lily_var *);
    return_doc(s, entry->doc_id);
}

static char *get_var_generics(lily_state *s, lily_var *v)
{
    /* Generics are stored after arguments. */
    uint16_t generic_spot = v->type->subtype_count;
    char **doc_data = s->gs->parser->doc->data[v->doc_id];
    char *result = doc_data[generic_spot];

    return result;
}

static void return_generics(lily_state *s, char *generic_str)
{
    /* Cache generics are in letter order. All this function needs to do is to
       iter the saved count of times. */
    lily_class **generics = s->gs->parser->generics->cache_generics;
    uint16_t count = (uint16_t)generic_str[0];
    lily_container_val *list_val = lily_push_list(s, count);
    uint16_t i;

    for (i = 0;i < count;i++) {
        lily_class *g = generics[i];
        lily_introspect_TypeEntry *new_type = INIT_TypeEntry(s);

        new_type->entry = g->self_type;
        lily_con_set_from_stack(s, list_val, i);
    }

    lily_return_top(s);
}

/**
define FunctionEntry.generics: List[TypeEntry]

Return the generic types available to this function. Functions defined outside
of manifest mode will always return `[]`.
*/
void lily_introspect_FunctionEntry_generics(lily_state *s)
{
    UNPACK_FIRST_ARG(FunctionEntry, lily_var *);

    if (entry->doc_id == (uint16_t)-1) {
        lily_push_list(s, 0);
        lily_return_top(s);
        return;
    }

    return_generics(s, get_var_generics(s, entry));
}

/**
define FunctionEntry.name: String

Return the name of the definition provided.
*/
void lily_introspect_FunctionEntry_name(lily_state *s)
{
    FETCH_FIELD(FunctionEntry, lily_var, const char *, name, lily_push_string);
}

/**
define FunctionEntry.line_number: Integer

Return the line number that this function was declared on.
*/
void lily_introspect_FunctionEntry_line_number(lily_state *s)
{
    lily_introspect_VarEntry_line_number(s);
}

static const char *get_block_string(char **block, int index)
{
    const char *str = "";

    if (block)
        str = block[index];

    return str;
}

/**
define FunctionEntry.parameters: List[ParameterEntry]

Return the parameters of this function. Functions processed outside of manifest
mode will have empty names.
*/
void lily_introspect_FunctionEntry_parameters(lily_state *s)
{
    UNPACK_FIRST_ARG(FunctionEntry, lily_var *);

    lily_type **types = entry->type->subtypes;
    lily_emit_state *emit = s->gs->parser->emit;
    char **keywords = lily_emit_proto_for_var(emit, entry)->keywords;
    char **doc = get_doc_text(s, entry->doc_id);
    uint16_t count = entry->type->subtype_count;
    uint16_t i;

    /* -1 to offset skipping the return at [0]. */
    lily_container_val *list_val = lily_push_list(s, count - 1);

    for (i = 1;i < count;i++) {
        lily_container_val *new_parameter = lily_push_instance(s,
                ID_ParameterEntry(s), 3);
        lily_introspect_TypeEntry *new_type = INIT_TypeEntry(s);

        /* No adjustment because the docblock and return match at 0. */
        lily_push_string(s, get_block_string(doc, i));
        SETFS_ParameterEntry__name(s, new_parameter);

        /* -1 because keywords start at 0. */
        lily_push_string(s, get_block_string(keywords, i - 1));
        SETFS_ParameterEntry__keyword(s, new_parameter);
        new_type->entry = types[i];
        SETFS_ParameterEntry__type(s, new_parameter);
        lily_con_set_from_stack(s, list_val, i - 1);
    }

    lily_return_top(s);
}

/**
define FunctionEntry.type: TypeEntry

Return the type of the definition provided.
*/
void lily_introspect_FunctionEntry_type(lily_state *s)
{
    unpack_and_return_type(s);
}

/**
foreign class MethodEntry {
    layout {
        lily_var *entry;
        lily_class *parent;
    }
}

This is a foreign class that wraps over a class or enum method.
*/

/**
define MethodEntry.function_name: String

Return the unqualified name of the function given.
*/
void lily_introspect_MethodEntry_function_name(lily_state *s)
{
    UNPACK_FIRST_ARG(MethodEntry, lily_var *);

    lily_push_string(s, entry->name);
    lily_return_top(s);
}

/**
define MethodEntry.generics: List[TypeEntry]

Return the generic types available to this method (including those from the
class/enum). Methods defined outside of manifest mode will always return `[]`.
*/
void lily_introspect_MethodEntry_generics(lily_state *s)
{
    lily_introspect_FunctionEntry_generics(s);
}

/**
define MethodEntry.line_number: Integer

Return the line number that this method was declared on.
*/
void lily_introspect_MethodEntry_line_number(lily_state *s)
{
    lily_introspect_VarEntry_line_number(s);
}

/**
define MethodEntry.is_private: Boolean

Return `true` if the method is private, `false` otherwise.
*/
void lily_introspect_MethodEntry_is_private(lily_state *s)
{
    UNPACK_FIRST_ARG(MethodEntry, lily_var *);
    lily_return_boolean(s, !!(entry->flags & SYM_SCOPE_PRIVATE));
}

/**
define MethodEntry.is_protected: Boolean

Return `true` if the method is protected, `false` otherwise.
*/
void lily_introspect_MethodEntry_is_protected(lily_state *s)
{
    UNPACK_FIRST_ARG(MethodEntry, lily_var *);
    lily_return_boolean(s, !!(entry->flags & SYM_SCOPE_PROTECTED));
}

/**
define MethodEntry.is_public: Boolean

Return `true` if the method is public, `false` otherwise.
*/
void lily_introspect_MethodEntry_is_public(lily_state *s)
{
    UNPACK_FIRST_ARG(MethodEntry, lily_var *);

    int flags = entry->flags & (SYM_SCOPE_PRIVATE | SYM_SCOPE_PROTECTED);

    lily_return_boolean(s, flags == 0);
}

/**
define MethodEntry.type: TypeEntry

Return the type of the method provided.
*/
void lily_introspect_MethodEntry_type(lily_state *s)
{
    unpack_and_return_type(s);
}

/**
foreign class ClassEntry {
    layout {
        lily_class *entry;
    }
}

This is a foreign class that wraps over a Lily class.
*/

/**
define ClassEntry.doc: String

Return the docblock of this class, or an empty string. Docblocks are only saved
when a class is parsed in manifest mode.
*/
void lily_introspect_ClassEntry_doc(lily_state *s)
{
    UNPACK_FIRST_ARG(ClassEntry, lily_class *);
    return_doc(s, entry->doc_id);
}

/**
define ClassEntry.generics: List[TypeEntry]

Return the generic types available to this class. Classes defined outside of
manifest mode will always return `[]`.
*/
void lily_introspect_ClassEntry_generics(lily_state *s)
{
    UNPACK_FIRST_ARG(ClassEntry, lily_class *);

    /* The first test isn't technically necessary right now since enough
       information is stored on the class. It's blocked anyway so that there's
       no regression in this function when generics become more exciting.
       The second test blocks magic classes (Function and Tuple), which have
       a count of -1 to denote that they take any amount. The lack of a cast on
       the second is intended, as the count is signed. */
    if (entry->doc_id == (uint16_t)-1 ||
        entry->generic_count == -1) {
        lily_push_list(s, 0);
        lily_return_top(s);
        return;
    }

    /* All other classes have a reasonable maximum that fits in any char. */
    char count = (char)entry->generic_count;
    char generic_str[] = {count, '\0'};

    return_generics(s, generic_str);
}

/**
define ClassEntry.is_foreign: Boolean

This is the opposite of `ClassEntry.is_native`.
*/
void lily_introspect_ClassEntry_is_foreign(lily_state *s)
{
    lily_introspect_ClassEntry *introspect_entry = ARG_ClassEntry(s, 0);
    lily_class *entry = introspect_entry->entry;
    int is_foreign = (entry->item_kind == ITEM_CLASS_FOREIGN);

    lily_return_boolean(s, is_foreign);
}

/**
define ClassEntry.is_native: Boolean

Returns 'true' if this class has properties and/or can be inherited. Most native
classes are found within native Lily modules. However, foreign libraries are
able to create native classes (ex: `Exception`).
*/
void lily_introspect_ClassEntry_is_native(lily_state *s)
{
    lily_introspect_ClassEntry *introspect_entry = ARG_ClassEntry(s, 0);
    lily_class *entry = introspect_entry->entry;
    int is_native = (entry->item_kind == ITEM_CLASS_NATIVE);

    lily_return_boolean(s, is_native);
}

/**
define ClassEntry.methods: List[MethodEntry]

Return the methods that were declared in this class. There is no guarantee as to
the order. The constructor's name is <new> to prevent it from being named.
*/
void lily_introspect_ClassEntry_methods(lily_state *s)
{
    lily_introspect_ClassEntry *introspect_entry = ARG_ClassEntry(s, 0);
    lily_class *entry = introspect_entry->entry;
    lily_named_sym *source = entry->members;
    lily_named_sym *source_iter = source;

    BUILD_LIST_FROM_2(allow_methods, make_method);
}

/**
define ClassEntry.module_path: String

Return the path of the module that this class belongs to.
*/
void lily_introspect_ClassEntry_module_path(lily_state *s)
{
    lily_introspect_ClassEntry *introspect_entry = ARG_ClassEntry(s, 0);
    lily_module_entry *m = introspect_entry->entry->module;

    lily_push_string(s, m->path);
    lily_return_top(s);
}

/**
define ClassEntry.name: String

Return the name of the class provided.
*/
void lily_introspect_ClassEntry_name(lily_state *s)
{
    FETCH_FIELD(ClassEntry, lily_class, const char *, name, lily_push_string);
}

/**
define ClassEntry.parent: Option[ClassEntry]

If this class inherits from another, this returns that class in a 'Some'.
Otherwise, this returns 'None'.
*/
void lily_introspect_ClassEntry_parent(lily_state *s)
{
    lily_introspect_ClassEntry *introspect_entry = ARG_ClassEntry(s, 0);
    lily_class *entry = introspect_entry->entry;
    lily_class *parent = entry->parent;

    if (parent) {
        lily_container_val *variant = lily_push_some(s);

        lily_introspect_ClassEntry *new_entry = INIT_ClassEntry(s);
        new_entry->entry = parent;

        lily_con_set_from_stack(s, variant, 0);
        lily_return_top(s);
    }
    else
        lily_return_none(s);
}

/**
define ClassEntry.properties: List[PropertyEntry]

Return the properties that were declared on the class provided. If a class has
been loaded, the properties inside are always loaded. This is in contrast to
methods which may not be loaded.
*/
void lily_introspect_ClassEntry_properties(lily_state *s)
{
    lily_introspect_ClassEntry *introspect_entry = ARG_ClassEntry(s, 0);
    lily_class *entry = introspect_entry->entry;
    lily_named_sym *source = entry->members;
    lily_named_sym *source_iter = source;

    BUILD_LIST_FROM_2(allow_properties, make_property);
}

/**
foreign class VariantEntry {
    layout {
        lily_variant_class *entry;
        lily_class *parent;
    }
}

This is a foreign class that wraps over an enum variant.
*/

/**
define VariantEntry.is_empty: Boolean

Returns true if the variant is empty, false otherwise. Empty variants are
variants that do not receive any values.
*/
void lily_introspect_VariantEntry_is_empty(lily_state *s)
{
    lily_introspect_VariantEntry *introspect_entry = ARG_VariantEntry(s, 0);
    lily_variant_class *entry = introspect_entry->entry;

    lily_return_boolean(s, entry->item_kind == ITEM_VARIANT_EMPTY);
}

/**
define VariantEntry.is_scoped: Boolean

Returns true if the variant is scoped, false otherwise. A variant is scoped if
the enum was prefixed with 'scoped' during declarations. Scoped variants must be
qualified with their names to be used, whereas flat variants are directly
available.
*/
void lily_introspect_VariantEntry_is_scoped(lily_state *s)
{
    lily_introspect_VariantEntry *introspect_entry = ARG_VariantEntry(s, 0);
    lily_class *parent = introspect_entry->parent;

    lily_return_boolean(s, parent->item_kind == ITEM_ENUM_SCOPED);
}

/**
define VariantEntry.module_path: String

Return the path of the module that this variant belongs to.
*/
void lily_introspect_VariantEntry_module_path(lily_state *s)
{
    lily_introspect_VariantEntry *introspect_entry = ARG_VariantEntry(s, 0);
    lily_module_entry *m = introspect_entry->entry->parent->module;

    lily_push_string(s, m->path);
    lily_return_top(s);
}

/**
define VariantEntry.name: String

Return the name of the variant provided.
*/
void lily_introspect_VariantEntry_name(lily_state *s)
{
    FETCH_FIELD(VariantEntry, lily_variant_class, const char *, name,
            lily_push_string);
}

/**
define VariantEntry.type: TypeEntry

Return the type of the method provided.
*/
void lily_introspect_VariantEntry_type(lily_state *s)
{
    unpack_and_return_type(s);
}

/**
foreign class EnumEntry {
    layout {
        lily_class *entry;
    }
}

This is a foreign class that wraps over an enum.
*/

/**
define EnumEntry.doc: String

Return the docblock of this enum, or an empty string. Docblocks are only saved
when an enum is parsed in manifest mode.
*/
void lily_introspect_EnumEntry_doc(lily_state *s)
{
    lily_introspect_ClassEntry_doc(s);
}

/**
define EnumEntry.generics: List[TypeEntry]

Return the generic types available to this enum. Enums defined outside of
manifest mode will always return `[]`.
*/
void lily_introspect_EnumEntry_generics(lily_state *s)
{
    lily_introspect_ClassEntry_generics(s);
}

/**
define EnumEntry.is_flat: Boolean

Returns true if the enum's variants are visible at toplevel, false otherwise.
*/
void lily_introspect_EnumEntry_is_flat(lily_state *s)
{
    UNPACK_FIRST_ARG(EnumEntry, lily_class *);
    lily_return_boolean(s, entry->item_kind == ITEM_ENUM_FLAT);
}

/**
define EnumEntry.is_scoped: Boolean

Returns true if the enum's variants are namespaced, false otherwise.
*/
void lily_introspect_EnumEntry_is_scoped(lily_state *s)
{
    UNPACK_FIRST_ARG(EnumEntry, lily_class *);
    lily_return_boolean(s, entry->item_kind == ITEM_ENUM_SCOPED);
}

/**
define EnumEntry.methods: List[MethodEntry]

Return the methods that were declared in this class. There is no guarantee as to
the order.
*/
void lily_introspect_EnumEntry_methods(lily_state *s)
{
    lily_introspect_EnumEntry *introspect_entry = ARG_EnumEntry(s, 0);
    lily_class *entry = introspect_entry->entry;
    lily_named_sym *source = entry->members;
    lily_named_sym *source_iter = source;

    BUILD_LIST_FROM_2(allow_methods, make_method);
}

/**
define EnumEntry.variants: List[VariantEntry]

Return the variants that were declared within this enum. No ordering is
guaranteed.
*/
void lily_introspect_EnumEntry_variants(lily_state *s)
{
    /* Variants come in two flavors:
       * Flat variants (like Some and None) are found in the module with the
         enum as their parent.
       * Scoped variants (the other kind) are found in the enum.
       The variants have the same item kind either way. */
    lily_introspect_EnumEntry *introspect_entry = ARG_EnumEntry(s, 0);
    lily_class *entry = introspect_entry->entry;
    int is_scoped = entry->item_kind == ITEM_ENUM_SCOPED;

    if (is_scoped) {
        lily_named_sym *source = entry->members;
        lily_named_sym *source_iter = source;

        BUILD_LIST_FROM_2(allow_scoped_variants, make_variant);
    }
    else {
        lily_named_sym *source = entry->members;
        lily_named_sym *source_iter = source;
        lily_class *parent = entry;

        BUILD_FLAT_VARIANT_LIST(allow_flat_variants, make_variant);
    }
}

/**
define EnumEntry.name: String

Return the name of the class provided.
*/
void lily_introspect_EnumEntry_name(lily_state *s)
{
    lily_introspect_ClassEntry_name(s);
}

/**
define EnumEntry.module_path: String

Return the path of the module that this enum belongs to.
*/
void lily_introspect_EnumEntry_module_path(lily_state *s)
{
    lily_introspect_ClassEntry_module_path(s);
}

/**
foreign class ModuleEntry {
    layout {
        lily_module_entry *entry;
    }
}

This is a foreign class that wraps over a module.
*/

/**
define ModuleEntry.boxed_classes: List[ClassEntry]

Return all classes that were directly imported into this module
(`import (someclass) somefile`).
*/
void lily_introspect_ModuleEntry_boxed_classes(lily_state *s)
{
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_boxed_sym *source = entry->boxed_chain;
    lily_boxed_sym *source_iter = source;

    BUILD_LIST_FROM(allow_boxed_classes, boxed_make_class)
}

/**
define ModuleEntry.boxed_enums: List[EnumEntry]

Return all enums that were directly imported into this module
(`import (someenum) somefile`).
*/
void lily_introspect_ModuleEntry_boxed_enums(lily_state *s)
{
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_boxed_sym *source = entry->boxed_chain;
    lily_boxed_sym *source_iter = source;

    BUILD_LIST_FROM(allow_boxed_enums, boxed_make_enum)
}

/**
define ModuleEntry.boxed_functions: List[FunctionEntry]

Return all functions that were directly imported into this module
(`import (somevariant) somefile`).
*/
void lily_introspect_ModuleEntry_boxed_functions(lily_state *s)
{
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_boxed_sym *source = entry->boxed_chain;
    lily_boxed_sym *source_iter = source;

    BUILD_LIST_FROM(allow_boxed_functions, boxed_make_function)
}

/**
define ModuleEntry.boxed_variants: List[VariantEntry]

Return all variants that were directly imported into this module
(`import (somevariant) somefile`).
*/
void lily_introspect_ModuleEntry_boxed_variants(lily_state *s)
{
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_boxed_sym *source = entry->boxed_chain;
    lily_boxed_sym *source_iter = source;

    BUILD_LIST_FROM(allow_boxed_variants, boxed_make_variant)
}

/**
define ModuleEntry.boxed_vars: List[VarEntry]

Return all vars that were directly imported into this module
(`import (somevar) somefile`).
*/
void lily_introspect_ModuleEntry_boxed_vars(lily_state *s)
{
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_boxed_sym *source = entry->boxed_chain;
    lily_boxed_sym *source_iter = source;

    BUILD_LIST_FROM(allow_boxed_vars, boxed_make_var)
}

/**
define ModuleEntry.classes: List[ClassEntry]

Return the classes declared within this module.
*/
void lily_introspect_ModuleEntry_classes(lily_state *s)
{
    /* Do not use boxed elements too. Those are another module's elements. */
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_class *source = entry->class_chain;
    lily_class *source_iter = source;

    BUILD_LIST_FROM(allow_classes, make_class)
}

/**
define ModuleEntry.dirname: String

Return the directory of this module relative to [main].
*/
void lily_introspect_ModuleEntry_dirname(lily_state *s)
{
    FETCH_FIELD_SAFE(ModuleEntry, lily_module_entry, const char *, dirname,
           lily_push_string, "");
}

/**
define ModuleEntry.id: Integer

Return a unique id that is based on the underlying module (not the instance).
If two 'ModuleEntry' instances both have the same underlying module, they will
have the same id.
*/
void lily_introspect_ModuleEntry_id(lily_state *s)
{
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);

    return_module_id(s, introspect_entry->entry);
}

/**
define ModuleEntry.enums: List[EnumEntry]

Return the enums declared within this module.
*/
void lily_introspect_ModuleEntry_enums(lily_state *s)
{
    /* Do not use boxed elements too. Those are another module's elements. */
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_class *source = entry->class_chain;
    lily_class *source_iter = source;

    BUILD_LIST_FROM(allow_enums, make_enum)
}

/**
define ModuleEntry.functions: List[FunctionEntry]

Return the functions that were declared inside of this module.
*/
void lily_introspect_ModuleEntry_functions(lily_state *s)
{
    /* Do not use boxed elements too. Those are another module's elements. */
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_var *source = entry->var_chain;
    lily_var *source_iter = source;

    BUILD_LIST_FROM(allow_functions, make_function)
}

/**
define ModuleEntry.modules_used: List[ModuleEntry]

Return the modules that were used inside of this module.
*/
void lily_introspect_ModuleEntry_modules_used(lily_state *s)
{
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_module_link *source = entry->module_chain;
    lily_module_link *source_iter = source;

    BUILD_LIST_FROM(allow_all, make_module_from_link);
}

/**
define ModuleEntry.name: String

Return the name of the module. A module's name is the default identifier used
when a module is imported by other modules.

Note: The origin module always has the name '[main]'.
*/
void lily_introspect_ModuleEntry_name(lily_state *s)
{
    FETCH_FIELD_SAFE(ModuleEntry, lily_module_entry, const char *, loadname,
            lily_push_string, "[main]");
}

/**
define ModuleEntry.path: String

Returns the path used to load the module. Registered modules and modules in the
prelude will have their name enclosed in brackets (ex: '[sys]').
*/
void lily_introspect_ModuleEntry_path(lily_state *s)
{
    FETCH_FIELD(ModuleEntry, lily_module_entry, const char *, path,
            lily_push_string);
}

/**
define ModuleEntry.vars: List[VarEntry]

Return the vars declared within this module.
*/
void lily_introspect_ModuleEntry_vars(lily_state *s)
{
    /* Do not use boxed elements too. Those are another module's elements. */
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_var *source = entry->var_chain;
    lily_var *source_iter = source;

    BUILD_LIST_FROM(allow_vars, make_var)
}

/**
define main_module: ModuleEntry

Returns the first module loaded.
*/
void lily_introspect__main_module(lily_state *s)
{
    lily_parse_state *parser = s->gs->parser;
    lily_module_entry *source = parser->main_module;

    make_module(s, source);
    lily_return_top(s);
}

/**
define module_list: List[ModuleEntry]

Return all modules inside of the interpreter. This includes registered modules
and modules in the interpreter's prelude. In most cases, `main_module` should be
used instead.
*/
void lily_introspect__module_list(lily_state *s)
{
    lily_parse_state *parser = s->gs->parser;
    lily_module_entry *source = parser->module_start;
    lily_module_entry *source_iter = source;

    BUILD_LIST_FROM(allow_all, make_module);
}

LILY_DECLARE_INTROSPECT_CALL_TABLE
