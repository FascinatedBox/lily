#ifndef LILY_SYMTAB_H
# define LILY_SYMTAB_H

# include "lily_core_types.h"
# include "lily_raiser.h"

typedef struct lily_symtab_ {
	/* This is a linked list of all vars that are currently in scope. The most
	   recently-declared one is at the top. */
    lily_var *var_chain;

    /* This is where __main__ is. __main__ is a special function which holds
       all of the code outside of a defined function. This is executed by the
       vm later to kick things off. */
    lily_var *main_var;

    /* The classes that are currently in scope. */
    lily_class *class_chain;

    /* To make generic class searches faster, the symtab holds the spot where
       the generic class. */
    lily_class *generic_class;

    /* Each literal (integer, double, string, etc) is stored in here.
       Additionally, variant psuedo-literals are stored here, so that basic
       variants (which do not have arguments) have a common, non-ref'd literal
       to represent them. */
    lily_tie *literals;

    /* Every function, be it foreign or native (that includes lambdas) is linked
       in here. The vm, during prep, will load these into a table and discard
       them.
       Though they're loaded into the same vm table as the literals, they're
       kept away from literals here so that literal scanning has less clutter to
       search through. */
    lily_tie *function_ties;

    /* This associates a global variable with a value. This is used by builtin
       packages, which need to save a value before the vm runs. */
    lily_tie *foreign_ties;

    /* Additionally, the signatures that are used to hold generic info are
       specifically linked together to make the search easier. */
    lily_type *generic_type_start;

    lily_import_entry *builtin_import;
    lily_import_entry *active_import;

    /* Defined functions that go out of scope are stuffed in here, unless
       they're class methods. */
    lily_var *old_function_chain;

    /* Ditto, for classes. */
    lily_class *old_class_chain;

    /* Every type created is linked in here some way. This makes tearing down
       types easy: Just iter through this and blast things along the way. */
    lily_type *root_type;

    /* The symtab keeps this because __main__ requires a special teardown. */
    lily_function_val *main_function;

    /* This is a linked list of symbols that were initially created through the
       vm or outside sources (via lily_symbol_by_name). These symbols can be
       garbage collected (hence the 'weak' in the type name). */
    lily_weak_symbol_entry *foreign_symbols;

    /* Each class gets a unique id. This is mostly for the builtin classes
       which have some special behavior sometimes. */
    uint32_t next_class_id;

    /* This is the spot where the next normal (non-define'd) var or storage will
       go. This is adjusted by emitter on function entry/exit. */
    uint32_t next_register_spot;

    /* This is the spot in the vm's readonly_table where the next 'readonly' var
       or literal will go. A 'readonly' var is a var that represents a lambda,
       built-in function, or native (define'd) function. */
    uint32_t next_readonly_spot;

    /* This is copied to vars when they're made. Parser uses this to figure out
       if a var is a local or a global. */
    uint32_t function_depth;

    /* When the symtab is in a package at the toplevel, then new vars use this
       for their register spot. This is so that all vars at the top of any
       package are created as globals. */
    uint32_t next_main_spot;

    /* This is adjusted whenever the emitter enters/leaves an import. This is
       used with function_depth figure out if new vars should use
       next_register_spot or next_main_spot for their register spot. */
    uint32_t import_depth;

    /* These classes are used frequently throughout the interpreter, so they're
       kept here for easy, fast access. */
    lily_class *integer_class;
    lily_class *double_class;
    lily_class *string_class;
    lily_class *bytestring_class;
    lily_class *symbol_class;
    lily_class *any_class;
    lily_class *function_class;
    lily_class *list_class;
    lily_class *hash_class;
    lily_class *tuple_class;
    lily_class *optarg_class;

    uint32_t *lex_linenum;
    lily_mem_func mem_func;
    lily_raiser *raiser;
} lily_symtab;

lily_symtab *lily_new_symtab(lily_options *, lily_import_entry *, lily_raiser *);
void lily_free_symtab(lily_symtab *);

lily_tie *lily_get_integer_literal(lily_symtab *, int64_t);
lily_tie *lily_get_double_literal(lily_symtab *, double);
lily_tie *lily_get_bytestring_literal(lily_symtab *, char *, int);
lily_tie *lily_get_string_literal(lily_symtab *, char *);
lily_tie *lily_get_symbol_literal(lily_symtab *, char *);
lily_tie *lily_get_variant_literal(lily_symtab *, lily_type *);

void lily_tie_builtin(lily_symtab *, lily_var *, lily_function_val *);
void lily_tie_function(lily_symtab *, lily_var *, lily_function_val *);
void lily_tie_value(lily_symtab *, lily_var *, lily_value *);

lily_symbol_val *lily_symbol_by_name(lily_symtab *, char *);
lily_class *lily_class_by_name(lily_symtab *, const char *);
lily_class *lily_class_by_name_within(lily_import_entry *, char *);
lily_var *lily_find_class_callable(lily_symtab *, lily_class *, char *);
const lily_func_seed *lily_find_class_call_seed(lily_symtab *, lily_class *,
        char *);
lily_prop_entry *lily_find_property(lily_symtab *, lily_class *, char *);
lily_class *lily_find_scoped_variant(lily_class *, char *);

lily_var *lily_new_var(lily_symtab *, lily_type *, char *, int);

lily_var *lily_var_by_name(lily_symtab *, char *);
lily_var *lily_var_by_name_within(lily_import_entry *, char *);

lily_type *lily_build_type(lily_symtab *, lily_class *, int, lily_type **, int, int);

void lily_hide_block_vars(lily_symtab *, lily_var *);
int lily_check_right_inherits_or_is(lily_class *, lily_class *);

lily_class *lily_new_class(lily_symtab *, char *);
lily_class *lily_new_class_by_seed(lily_symtab *, lily_class_seed);
lily_class *lily_new_variant_class(lily_symtab *, lily_class *, char *);
void lily_finish_variant_class(lily_symtab *, lily_class *, lily_type *);
void lily_add_class_method(lily_symtab *, lily_class *, lily_var *);

lily_prop_entry *lily_add_class_property(lily_symtab *, lily_class *,
        lily_type *, char *, int);
void lily_update_symtab_generics(lily_symtab *, lily_class *, int);
void lily_finish_class(lily_symtab *, lily_class *);
void lily_make_constructor_return_type(lily_symtab *);
void lily_finish_enum_class(lily_symtab *, lily_class *, int, lily_type *);
void lily_change_parent_class(lily_class *, lily_class *);

void lily_enter_import(lily_symtab *, lily_import_entry *);
void lily_leave_import(lily_symtab *);
lily_import_entry *lily_find_import(lily_symtab *, char *);
lily_import_entry *lily_find_import_within(lily_import_entry *, char *);
lily_import_entry *lily_find_import_anywhere(lily_symtab *, char *);
void lily_link_import_to_active(lily_symtab *, lily_import_entry *);

const lily_func_seed *lily_get_global_seed_chain();
#endif
