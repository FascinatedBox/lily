#ifndef LILY_SYMTAB_H
# define LILY_SYMTAB_H

# include "lily_core_types.h"
# include "lily_value.h"

typedef struct lily_value_stack_ {
    lily_value **data;
    uint16_t pos;
    uint16_t size;
    uint32_t pad;
} lily_value_stack;

typedef struct lily_symtab_ {
    lily_module_entry *prelude_module;
    lily_module_entry *active_module;

    /* Defined functions that go out of scope are stuffed in here, unless
       they're class methods. */
    lily_var *hidden_function_chain;

    /* Ditto, for classes. */
    lily_class *hidden_class_chain;

    lily_value_stack *literals;

    /* Each class gets a unique id. This is mostly for the builtin classes
       which have some special behavior sometimes. */
    uint16_t next_class_id;

    uint16_t next_global_id;

    uint32_t pad;

    /* These classes are used frequently throughout the interpreter, so they're
       kept here for easy, fast access. */
    lily_class *integer_class;
    lily_class *double_class;
    lily_class *string_class;
    lily_class *byte_class;
    lily_class *bytestring_class;
    lily_class *boolean_class;
    lily_class *function_class;
    lily_class *list_class;
    lily_class *hash_class;
    lily_class *tuple_class;
    lily_class *optarg_class;
} lily_symtab;

lily_symtab *lily_new_symtab(void);
void lily_set_prelude(lily_symtab *, lily_module_entry *);
void lily_free_module_symbols(lily_symtab *, lily_module_entry *);
void lily_free_properties(lily_class *);
void lily_rewind_symtab(lily_symtab *, lily_module_entry *, lily_class *,
        lily_var *, lily_boxed_sym *, int);
void lily_free_symtab(lily_symtab *);

lily_value *lily_literal_at(lily_symtab *, uint16_t);
lily_literal *lily_get_integer_literal(lily_symtab *, lily_type **, int64_t);
lily_literal *lily_get_double_literal(lily_symtab *, lily_type **, double);
lily_literal *lily_get_bytestring_literal(lily_symtab *, lily_type **,
        const char *, uint32_t);
lily_literal *lily_get_string_literal(lily_symtab *, lily_type **,
        const char *);
lily_literal *lily_get_unit_literal(lily_symtab *);
void lily_new_function_literal(lily_symtab *, lily_var *, lily_value *);

lily_class *lily_find_class(lily_module_entry *, const char *);
lily_var *lily_find_var(lily_module_entry *, const char *);
lily_named_sym *lily_find_member(lily_class *, const char *);
lily_named_sym *lily_find_member_in_class(lily_class *, const char *);
lily_variant_class *lily_find_variant(lily_class *, const char *);
lily_module_entry *lily_find_module(lily_module_entry *, const char *);
lily_module_entry *lily_find_module_by_path(lily_symtab *, const char *);
lily_module_entry *lily_find_registered_module(lily_symtab *, const char *);

lily_generic_class *lily_new_generic_class(const char *);
lily_class *lily_new_raw_class(const char *, uint16_t);
lily_class *lily_new_class(lily_symtab *, const char *, uint16_t);
lily_class *lily_new_enum_class(lily_symtab *, const char *, uint16_t);
lily_variant_class *lily_new_variant_class(lily_class *, const char *,
        uint16_t);

lily_prop_entry *lily_add_class_property(lily_class *, lily_type *,
        const char *, uint16_t, uint16_t);
void lily_add_symbol_ref(lily_module_entry *, lily_sym *);

void lily_fix_enum_variant_ids(lily_symtab *, lily_class *);
void lily_fix_enum_type_ids(lily_class *);
void lily_register_classes(lily_symtab *, struct lily_vm_state_ *);
#endif
