#include "lily_vm.h"
#include "lily_parser.h"
#include "lily_core_types.h"
#include "lily_symtab.h"

#include "lily.h"
#define LILY_NO_EXPORT
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

typedef struct {
    LILY_FOREIGN_HEADER
    lily_class *entry;
} lily_introspect_ClassEntry;

typedef struct {
    LILY_FOREIGN_HEADER
    lily_class *entry;
} lily_introspect_EnumEntry;

typedef struct {
    LILY_FOREIGN_HEADER
    lily_var *entry;
} lily_introspect_FunctionEntry;

typedef struct {
    LILY_FOREIGN_HEADER
    lily_var *entry;
    lily_class *parent;
} lily_introspect_MethodEntry;

typedef struct {
    LILY_FOREIGN_HEADER
    lily_module_entry *entry;
} lily_introspect_ModuleEntry;

typedef struct {
    LILY_FOREIGN_HEADER
    lily_type *entry;
} lily_introspect_TypeEntry;

typedef struct {
    LILY_FOREIGN_HEADER
    lily_var *entry;
} lily_introspect_VarEntry;

typedef struct {
    LILY_FOREIGN_HEADER
    lily_variant_class *entry;
    lily_class *parent;
} lily_introspect_VariantEntry;

typedef struct {
    LILY_FOREIGN_HEADER
    lily_prop_entry *entry;
    lily_class *parent;
} lily_introspect_PropertyEntry;

static void destroy_ClassEntry(lily_introspect_ClassEntry *c)
{
    (void)c;
}

static void destroy_EnumEntry(lily_introspect_EnumEntry *c)
{
    (void)c;
}

static void destroy_FunctionEntry(lily_introspect_FunctionEntry *f)
{
    (void)f;
}

static void destroy_MethodEntry(lily_introspect_MethodEntry *m)
{
    (void)m;
}

static void destroy_ModuleEntry(lily_introspect_ModuleEntry *m)
{
    (void)m;
}

static void destroy_PropertyEntry(lily_introspect_PropertyEntry *p)
{
    (void)p;
}

static void destroy_VarEntry(lily_introspect_VarEntry *v)
{
    (void)v;
}

static void destroy_VariantEntry(lily_introspect_VariantEntry *v)
{
    (void)v;
}

static void destroy_TypeEntry(lily_introspect_TypeEntry *t)
{
    (void)t;
}

static int allow_all(void *any)
{
    (void)any;

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

static void unpack_and_return_result_type(lily_state *s)
{
    UNPACK_FIRST_ARG(FunctionEntry, lily_var *);
    lily_type *t = entry->type;

    lily_introspect_TypeEntry *boxed_type = INIT_TypeEntry(s);
    boxed_type->entry = t->subtypes[0];

    lily_return_top(s);
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

void lily_introspect_TypeEntry_as_string(lily_state *s)
{
    UNPACK_FIRST_ARG(TypeEntry, lily_type *);
    lily_msgbuf *msgbuf = lily_mb_flush(lily_msgbuf_get(s));

    lily_mb_add_fmt(msgbuf, "^T", entry);
    lily_push_string(s, lily_mb_raw(msgbuf));
    lily_return_top(s);
}

void lily_introspect_TypeEntry_class_name(lily_state *s)
{
    FETCH_FIELD(TypeEntry, lily_type, const char *, cls->name,
            lily_push_string);
}

void lily_introspect_TypeEntry_class_id(lily_state *s)
{
    UNPACK_FIRST_ARG(TypeEntry, lily_type *);
    lily_return_integer(s, entry->cls->id);
}

void lily_introspect_TypeEntry_inner_types(lily_state *s)
{
    UNPACK_FIRST_ARG(TypeEntry, lily_type *);

    /* Function's behavior is due to how parser builds Function types. */
    uint16_t count = entry->subtype_count;
    lily_type **inner_types = entry->subtypes;
    lily_container_val *list_val = lily_push_list(s, count);
    uint16_t i;

    for (i = 0;i < count;i++) {
        lily_introspect_TypeEntry *new_type = INIT_TypeEntry(s);

        new_type->entry = inner_types[i];
        lily_con_set_from_stack(s, list_val, i);
    }

    lily_return_top(s);
}

void lily_introspect_TypeEntry_is_vararg_function(lily_state *s)
{
    UNPACK_FIRST_ARG(TypeEntry, lily_type *);

    lily_return_boolean(s, !!(entry->flags & TYPE_IS_VARARGS));
}

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

void lily_introspect_VarEntry_doc(lily_state *s)
{
    UNPACK_FIRST_ARG(VarEntry, lily_var *);
    return_doc(s, entry->doc_id);
}

void lily_introspect_VarEntry_line_number(lily_state *s)
{
    lily_introspect_VarEntry *introspect_entry = ARG_VarEntry(s, 0);
    lily_var *entry = introspect_entry->entry;
    lily_return_integer(s, entry->line_num);
}

void lily_introspect_VarEntry_name(lily_state *s)
{
    FETCH_FIELD(VarEntry, lily_var, const char *, name, lily_push_string);
}

void lily_introspect_VarEntry_type(lily_state *s)
{
    unpack_and_return_type(s);
}

void lily_introspect_PropertyEntry_doc(lily_state *s)
{
    UNPACK_FIRST_ARG(PropertyEntry, lily_prop_entry *);
    return_doc(s, entry->doc_id);
}

void lily_introspect_PropertyEntry_is_private(lily_state *s)
{
    UNPACK_FIRST_ARG(PropertyEntry, lily_prop_entry *);
    lily_return_boolean(s, !!(entry->flags & SYM_SCOPE_PRIVATE));
}

void lily_introspect_PropertyEntry_is_protected(lily_state *s)
{
    UNPACK_FIRST_ARG(PropertyEntry, lily_prop_entry *);
    lily_return_boolean(s, !!(entry->flags & SYM_SCOPE_PROTECTED));
}

void lily_introspect_PropertyEntry_is_public(lily_state *s)
{
    UNPACK_FIRST_ARG(PropertyEntry, lily_prop_entry *);
    lily_return_boolean(s, !!(entry->flags & SYM_SCOPE_PUBLIC));
}

void lily_introspect_PropertyEntry_name(lily_state *s)
{
    FETCH_FIELD(PropertyEntry, lily_prop_entry, const char *, name, lily_push_string);
}

void lily_introspect_PropertyEntry_type(lily_state *s)
{
    unpack_and_return_type(s);
}

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

void lily_introspect_FunctionEntry_name(lily_state *s)
{
    FETCH_FIELD(FunctionEntry, lily_var, const char *, name, lily_push_string);
}

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

static void push_parameters(lily_state *s, lily_type *type, char **doc,
                            char **keywords)
{
    lily_type **types = type->subtypes;
    uint16_t count = type->subtype_count;
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

void lily_introspect_FunctionEntry_parameters(lily_state *s)
{
    UNPACK_FIRST_ARG(FunctionEntry, lily_var *);

    lily_emit_state *emit = s->gs->parser->emit;
    lily_type *type = entry->type;
    char **doc = get_doc_text(s, entry->doc_id);
    char **keywords = lily_emit_proto_for_var(emit, entry)->keywords;

    push_parameters(s, type, doc, keywords);
}

void lily_introspect_FunctionEntry_result_type(lily_state *s)
{
    unpack_and_return_result_type(s);
}

void lily_introspect_FunctionEntry_type(lily_state *s)
{
    unpack_and_return_type(s);
}

void lily_introspect_MethodEntry_doc(lily_state *s)
{
    UNPACK_FIRST_ARG(FunctionEntry, lily_var *);
    return_doc(s, entry->doc_id);
}

void lily_introspect_MethodEntry_function_name(lily_state *s)
{
    UNPACK_FIRST_ARG(MethodEntry, lily_var *);

    lily_push_string(s, entry->name);
    lily_return_top(s);
}

void lily_introspect_MethodEntry_generics(lily_state *s)
{
    lily_introspect_FunctionEntry_generics(s);
}

void lily_introspect_MethodEntry_line_number(lily_state *s)
{
    lily_introspect_VarEntry_line_number(s);
}

void lily_introspect_MethodEntry_is_private(lily_state *s)
{
    UNPACK_FIRST_ARG(MethodEntry, lily_var *);
    lily_return_boolean(s, !!(entry->flags & SYM_SCOPE_PRIVATE));
}

void lily_introspect_MethodEntry_is_protected(lily_state *s)
{
    UNPACK_FIRST_ARG(MethodEntry, lily_var *);
    lily_return_boolean(s, !!(entry->flags & SYM_SCOPE_PROTECTED));
}

void lily_introspect_MethodEntry_is_public(lily_state *s)
{
    UNPACK_FIRST_ARG(MethodEntry, lily_var *);

    int flags = entry->flags & (SYM_SCOPE_PRIVATE | SYM_SCOPE_PROTECTED);

    lily_return_boolean(s, flags == 0);
}

void lily_introspect_MethodEntry_is_static(lily_state *s)
{
    UNPACK_FIRST_ARG(MethodEntry, lily_var *);
    lily_return_boolean(s, !!(entry->flags & VAR_IS_STATIC));
}

void lily_introspect_MethodEntry_parameters(lily_state *s)
{
    lily_introspect_FunctionEntry_parameters(s);
}

void lily_introspect_MethodEntry_result_type(lily_state *s)
{
    unpack_and_return_result_type(s);
}

void lily_introspect_MethodEntry_type(lily_state *s)
{
    unpack_and_return_type(s);
}

void lily_introspect_ClassEntry_doc(lily_state *s)
{
    UNPACK_FIRST_ARG(ClassEntry, lily_class *);
    return_doc(s, entry->doc_id);
}

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

void lily_introspect_ClassEntry_id(lily_state *s)
{
    UNPACK_FIRST_ARG(ClassEntry, lily_class *);

    lily_return_integer(s, entry->id);
}

void lily_introspect_ClassEntry_is_foreign(lily_state *s)
{
    lily_introspect_ClassEntry *introspect_entry = ARG_ClassEntry(s, 0);
    lily_class *entry = introspect_entry->entry;
    int is_foreign = (entry->item_kind == ITEM_CLASS_FOREIGN);

    lily_return_boolean(s, is_foreign);
}

void lily_introspect_ClassEntry_is_native(lily_state *s)
{
    lily_introspect_ClassEntry *introspect_entry = ARG_ClassEntry(s, 0);
    lily_class *entry = introspect_entry->entry;
    int is_native = (entry->item_kind == ITEM_CLASS_NATIVE);

    lily_return_boolean(s, is_native);
}

void lily_introspect_ClassEntry_methods(lily_state *s)
{
    lily_introspect_ClassEntry *introspect_entry = ARG_ClassEntry(s, 0);
    lily_class *entry = introspect_entry->entry;
    lily_named_sym *source = entry->members;
    lily_named_sym *source_iter = source;

    BUILD_LIST_FROM_2(allow_methods, make_method);
}

void lily_introspect_ClassEntry_name(lily_state *s)
{
    FETCH_FIELD(ClassEntry, lily_class, const char *, name, lily_push_string);
}

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

void lily_introspect_ClassEntry_properties(lily_state *s)
{
    lily_introspect_ClassEntry *introspect_entry = ARG_ClassEntry(s, 0);
    lily_class *entry = introspect_entry->entry;
    lily_named_sym *source = entry->members;
    lily_named_sym *source_iter = source;

    BUILD_LIST_FROM_2(allow_properties, make_property);
}

void lily_introspect_VariantEntry_enum_id(lily_state *s)
{
    UNPACK_FIRST_ARG(VariantEntry, lily_variant_class *);

    lily_return_integer(s, entry->parent->id);
}

void lily_introspect_VariantEntry_enum_name(lily_state *s)
{
    UNPACK_FIRST_ARG(VariantEntry, lily_variant_class *);

    lily_push_string(s, entry->parent->name);
    lily_return_top(s);
}

void lily_introspect_VariantEntry_is_empty(lily_state *s)
{
    lily_introspect_VariantEntry *introspect_entry = ARG_VariantEntry(s, 0);
    lily_variant_class *entry = introspect_entry->entry;

    lily_return_boolean(s, entry->item_kind == ITEM_VARIANT_EMPTY);
}

void lily_introspect_VariantEntry_is_scoped(lily_state *s)
{
    lily_introspect_VariantEntry *introspect_entry = ARG_VariantEntry(s, 0);
    lily_class *parent = introspect_entry->parent;

    lily_return_boolean(s, parent->item_kind == ITEM_ENUM_SCOPED);
}

void lily_introspect_VariantEntry_name(lily_state *s)
{
    FETCH_FIELD(VariantEntry, lily_variant_class, const char *, name,
            lily_push_string);
}

void lily_introspect_VariantEntry_parameters(lily_state *s)
{
    UNPACK_FIRST_ARG(VariantEntry, lily_variant_class *);

    lily_type *type = entry->build_type;

    /* The build type of a variant depends on if it takes arguments:

       Variants with arguments will have a Function that returns an enum with
       any generics known filled in.

       Variants without arguments will return their enum parent with ? in place
       of any generics. */
    if (type->cls->id != LILY_ID_FUNCTION) {
        lily_push_list(s, 0);
        lily_return_top(s);
        return;
    }

    char **doc = NULL;
    char **keywords = entry->keywords;

    push_parameters(s, type, doc, keywords);
}

void lily_introspect_VariantEntry_type(lily_state *s)
{
    unpack_and_return_type(s);
}

void lily_introspect_EnumEntry_doc(lily_state *s)
{
    lily_introspect_ClassEntry_doc(s);
}

void lily_introspect_EnumEntry_generics(lily_state *s)
{
    lily_introspect_ClassEntry_generics(s);
}

void lily_introspect_EnumEntry_id(lily_state *s)
{
    lily_introspect_ClassEntry_id(s);
}

void lily_introspect_EnumEntry_is_flat(lily_state *s)
{
    UNPACK_FIRST_ARG(EnumEntry, lily_class *);
    lily_return_boolean(s, entry->item_kind == ITEM_ENUM_FLAT);
}

void lily_introspect_EnumEntry_is_scoped(lily_state *s)
{
    UNPACK_FIRST_ARG(EnumEntry, lily_class *);
    lily_return_boolean(s, entry->item_kind == ITEM_ENUM_SCOPED);
}

void lily_introspect_EnumEntry_methods(lily_state *s)
{
    lily_introspect_EnumEntry *introspect_entry = ARG_EnumEntry(s, 0);
    lily_class *entry = introspect_entry->entry;
    lily_named_sym *source = entry->members;
    lily_named_sym *source_iter = source;

    BUILD_LIST_FROM_2(allow_methods, make_method);
}

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

void lily_introspect_EnumEntry_name(lily_state *s)
{
    lily_introspect_ClassEntry_name(s);
}

void lily_introspect_ModuleEntry_boxed_classes(lily_state *s)
{
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_boxed_sym *source = entry->boxed_chain;
    lily_boxed_sym *source_iter = source;

    BUILD_LIST_FROM(allow_boxed_classes, boxed_make_class)
}

void lily_introspect_ModuleEntry_boxed_enums(lily_state *s)
{
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_boxed_sym *source = entry->boxed_chain;
    lily_boxed_sym *source_iter = source;

    BUILD_LIST_FROM(allow_boxed_enums, boxed_make_enum)
}

void lily_introspect_ModuleEntry_boxed_functions(lily_state *s)
{
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_boxed_sym *source = entry->boxed_chain;
    lily_boxed_sym *source_iter = source;

    BUILD_LIST_FROM(allow_boxed_functions, boxed_make_function)
}

void lily_introspect_ModuleEntry_boxed_variants(lily_state *s)
{
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_boxed_sym *source = entry->boxed_chain;
    lily_boxed_sym *source_iter = source;

    BUILD_LIST_FROM(allow_boxed_variants, boxed_make_variant)
}

void lily_introspect_ModuleEntry_boxed_vars(lily_state *s)
{
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_boxed_sym *source = entry->boxed_chain;
    lily_boxed_sym *source_iter = source;

    BUILD_LIST_FROM(allow_boxed_vars, boxed_make_var)
}

void lily_introspect_ModuleEntry_classes(lily_state *s)
{
    /* Do not use boxed elements too. Those are another module's elements. */
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_class *source = entry->class_chain;
    lily_class *source_iter = source;

    BUILD_LIST_FROM(allow_classes, make_class)
}

void lily_introspect_ModuleEntry_dirname(lily_state *s)
{
    FETCH_FIELD_SAFE(ModuleEntry, lily_module_entry, const char *, dirname,
           lily_push_string, "");
}

void lily_introspect_ModuleEntry_doc(lily_state *s)
{
    UNPACK_FIRST_ARG(ModuleEntry, lily_module_entry *);
    return_doc(s, entry->doc_id);
}

void lily_introspect_ModuleEntry_enums(lily_state *s)
{
    /* Do not use boxed elements too. Those are another module's elements. */
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_class *source = entry->class_chain;
    lily_class *source_iter = source;

    BUILD_LIST_FROM(allow_enums, make_enum)
}

void lily_introspect_ModuleEntry_functions(lily_state *s)
{
    /* Do not use boxed elements too. Those are another module's elements. */
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_var *source = entry->var_chain;
    lily_var *source_iter = source;

    BUILD_LIST_FROM(allow_functions, make_function)
}

void lily_introspect_ModuleEntry_modules_used(lily_state *s)
{
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_module_link *source = entry->module_chain;
    lily_module_link *source_iter = source;

    BUILD_LIST_FROM(allow_all, make_module_from_link);
}

void lily_introspect_ModuleEntry_name(lily_state *s)
{
    FETCH_FIELD_SAFE(ModuleEntry, lily_module_entry, const char *, loadname,
            lily_push_string, "[main]");
}

void lily_introspect_ModuleEntry_path(lily_state *s)
{
    FETCH_FIELD(ModuleEntry, lily_module_entry, const char *, path,
            lily_push_string);
}

void lily_introspect_ModuleEntry_vars(lily_state *s)
{
    /* Do not use boxed elements too. Those are another module's elements. */
    lily_introspect_ModuleEntry *introspect_entry = ARG_ModuleEntry(s, 0);
    lily_module_entry *entry = introspect_entry->entry;
    lily_var *source = entry->var_chain;
    lily_var *source_iter = source;

    BUILD_LIST_FROM(allow_vars, make_var)
}

void lily_introspect__main_module(lily_state *s)
{
    lily_parse_state *parser = s->gs->parser;
    lily_module_entry *source = parser->main_module;

    make_module(s, source);
    lily_return_top(s);
}

void lily_introspect__module_list(lily_state *s)
{
    lily_parse_state *parser = s->gs->parser;
    lily_module_entry *source = parser->module_start;
    lily_module_entry *source_iter = source;

    BUILD_LIST_FROM(allow_all, make_module);
}

LILY_DECLARE_INTROSPECT_CALL_TABLE
