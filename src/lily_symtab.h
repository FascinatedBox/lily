#ifndef LILY_SYMTAB_H
# define LILY_SYMTAB_H

# include "lily_core_types.h"
# include "lily_raiser.h"

typedef struct {
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
       the template class. */
    lily_class *template_class;

    /* A linked list of all literals that have been found so far. */
    lily_literal *lit_chain;

    /* Additionally, the signatures that are used to hold generic info are
       specifically linked together to make the search easier. */
    lily_type *template_type_start;

    /* Defined functions that go out of scope are stuffed in here, unless
       they're class methods. */
    lily_var *old_function_chain;

    /* Ditto, for classes. */
    lily_class *old_class_chain;

    /* Every type created is linked in here some way. This makes tearing down
       types easy: Just iter through this and blast things along the way. */
    lily_type *root_type;

    /* Each class gets a unique id. This is mostly for the builtin classes
       which have some special behavior sometimes. */
    uint32_t next_class_id;

    /* Lily works on the concept of 'registers'. Each var in a function has a
       specific id where it goes for loading/storing.
       The first one is adjusted by emitter when entering or leaving a
       function. */
    uint32_t next_register_spot;
    uint32_t next_lit_spot;
    uint32_t next_function_spot;

    /* This is copied to vars when they're made. Parser uses this to figure out
       if a var is a local or a global. */
    uint64_t function_depth;

    uint16_t *lex_linenum;
    lily_raiser *raiser;
} lily_symtab;

lily_symtab *lily_new_symtab(lily_raiser *);
void lily_free_symtab_lits_and_vars(lily_symtab *);
void lily_free_symtab(lily_symtab *);

lily_literal *lily_get_integer_literal(lily_symtab *, int64_t);
lily_literal *lily_get_double_literal(lily_symtab *, double);
lily_literal *lily_get_string_literal(lily_symtab *, char *);
lily_literal *lily_get_variant_literal(lily_symtab *, lily_type *);

lily_class *lily_class_by_id(lily_symtab *, int);
lily_class *lily_class_by_name(lily_symtab *, const char *);
lily_var *lily_find_class_callable(lily_symtab *, lily_class *, char *);
const lily_func_seed *lily_find_class_call_seed(lily_symtab *, lily_class *,
        char *);
lily_prop_entry *lily_find_property(lily_symtab *, lily_class *, char *);
lily_class *lily_find_scoped_variant(lily_class *, char *);

lily_var *lily_try_new_var(lily_symtab *, lily_type *, char *, int);

lily_var *lily_scoped_var_by_name(lily_symtab *, lily_var *, char *);
lily_var *lily_var_by_name(lily_symtab *, char *);

lily_type *lily_try_type_for_class(lily_symtab *, lily_class *);
lily_type *lily_try_type_from_ids(lily_symtab *, const int *);
lily_type *lily_build_ensure_type(lily_symtab *, lily_class *, int, lily_type **, int, int);

void lily_hide_block_vars(lily_symtab *, lily_var *);
int lily_check_right_inherits_or_is(lily_class *, lily_class *);

lily_class *lily_new_class(lily_symtab *, char *);
void lily_add_variant_class(lily_symtab *, lily_class *, char *, lily_type *);
lily_prop_entry *lily_add_class_property(lily_class *, lily_type *, char *, int);
void lily_update_symtab_generics(lily_symtab *, lily_class *, int);
void lily_finish_class(lily_symtab *, lily_class *);
void lily_make_constructor_return_type(lily_symtab *);
void lily_finish_enum_class(lily_symtab *, lily_class *, int, lily_type *);

const lily_func_seed *lily_get_global_seed_chain();
#endif
