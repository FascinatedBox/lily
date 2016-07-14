#ifndef LILY_SYMTAB_H
# define LILY_SYMTAB_H

# include "lily_core_types.h"
# include "lily_value_structs.h"
# include "lily_value_stack.h"

typedef struct lily_symtab_ {
    /* This is where __main__ is. __main__ is a special function which holds
       all of the code outside of a defined function. This is executed by the
       vm later to kick things off. */
    lily_var *main_var;

    lily_value_stack *literals;

    lily_package *first_package;

    lily_module_entry *builtin_module;
    lily_module_entry *active_module;

    /* Defined functions that go out of scope are stuffed in here, unless
       they're class methods. */
    lily_var *old_function_chain;

    /* Ditto, for classes. */
    lily_class *old_class_chain;

    /* The symtab keeps this because __main__ requires a special teardown. */
    lily_function_val *main_function;

    /* Each class gets a unique id. This is mostly for the builtin classes
       which have some special behavior sometimes. */
    uint32_t next_class_id;

    uint32_t pad;

    /* These classes are used frequently throughout the interpreter, so they're
       kept here for easy, fast access. */
    lily_class *integer_class;
    lily_class *double_class;
    lily_class *string_class;
    lily_class *bytestring_class;
    lily_class *boolean_class;
    lily_class *dynamic_class;
    lily_class *function_class;
    lily_class *list_class;
    lily_class *hash_class;
    lily_class *tuple_class;
    lily_class *optarg_class;
    lily_class *generic_class;
    lily_class *question_class;

    uint32_t *lex_linenum;
} lily_symtab;

lily_symtab *lily_new_symtab(void);
void lily_set_first_package(lily_symtab *, lily_package *);
void lily_free_symtab(lily_symtab *);

lily_literal *lily_get_integer_literal(lily_symtab *, int64_t);
lily_literal *lily_get_double_literal(lily_symtab *, double);
lily_literal *lily_get_bytestring_literal(lily_symtab *, const char *, int);
lily_literal *lily_get_string_literal(lily_symtab *, const char *);
void lily_store_function(lily_symtab *, lily_var *, lily_function_val *);
void lily_store_builtin(lily_symtab *, lily_var *, lily_function_val *);

lily_class *lily_find_class(lily_symtab *, lily_module_entry *, const char *);
lily_var *lily_find_method(lily_class *, const char *);
lily_prop_entry *lily_find_property(lily_class *, const char *);
lily_variant_class *lily_find_scoped_variant(lily_class *, const char *);
lily_named_sym *lily_find_member(lily_class *, const char *);

lily_var *lily_new_raw_var(lily_symtab *, lily_type *, const char *);
lily_var *lily_new_raw_unlinked_var(lily_symtab *, lily_type *, const char *);
lily_var *lily_find_var(lily_symtab *, lily_module_entry *, const char *);

lily_type *lily_build_type(lily_symtab *, lily_class *, int, lily_type **, int, int);

void lily_hide_block_vars(lily_symtab *, lily_var *);

lily_class *lily_new_raw_class(lily_symtab *, const char *);
lily_class *lily_new_class(lily_symtab *, const char *);
lily_class *lily_new_enum(lily_symtab *, const char *);
lily_variant_class *lily_new_variant(lily_symtab *, lily_class *, const char *,
        int);
void lily_add_class_method(lily_symtab *, lily_class *, lily_var *);

lily_prop_entry *lily_add_class_property(lily_symtab *, lily_class *,
        lily_type *, const char *, int);
void lily_update_symtab_generics(lily_symtab *, int);
void lily_finish_enum(lily_symtab *, lily_class *, int, lily_type *);

void lily_register_classes(lily_symtab *, struct lily_vm_state_ *);

lily_module_entry *lily_find_module(lily_symtab *, lily_module_entry *,
        const char *);
lily_module_entry *lily_find_module_by_path(lily_package *, const char *);
lily_package *lily_find_package(lily_module_entry *, const char *);
#endif
