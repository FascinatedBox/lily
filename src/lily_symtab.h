#ifndef LILY_SYMTAB_H
# define LILY_SYMTAB_H

# include "lily_core_types.h"
# include "lily_raiser.h"

typedef struct {
    lily_var *var_chain;
    lily_var *main_var;

    lily_class *template_class;
    lily_sig *template_sig_start;

    /* When a function has functions declared inside of it, those functions
       fall out of scope when the other function goes out of scope. The inner
       functions end up going in here. This makes the inner functions no longer
       reachable, but keeps them alive (for obvious reasons)!
       Emitter adds to this as needed. Symtab is only responsible for ensuring
       that it's destroyed properly at exit. */
    lily_var *old_function_chain;
    /* Similar to the above: A class declared in another class goes out of
       scope when the outer class is done. */
    lily_class *old_class_chain;

    lily_class *class_chain;
    lily_sig *root_sig;

    int function_depth;

    lily_literal *lit_chain;
    int next_class_id;
    int next_register_spot;
    int next_lit_spot;
    int next_function_spot;
    uint16_t *lex_linenum;
    lily_raiser *raiser;
} lily_symtab;

/* Sync with keywords in lily_seed_symtab.h. */
#define KEY_IF          0
#define KEY_ELIF        1
#define KEY_ELSE        2
#define KEY_RETURN      3
#define KEY_WHILE       4
#define KEY_CONTINUE    5
#define KEY_BREAK       6
#define KEY__LINE__     7
#define KEY__FILE__     8
#define KEY__FUNCTION__ 9
#define KEY_FOR         10
#define KEY_DO          11
#define KEY_ISNIL       12
#define KEY_TRY         13
#define KEY_EXCEPT      14
#define KEY_RAISE       15
#define KEY_CLASS       16
#define KEY_VAR         17
#define KEY_ENUM        18
#define KEY_LAST_ID     18

lily_symtab *lily_new_symtab(lily_raiser *);
void lily_free_symtab_lits_and_vars(lily_symtab *);
void lily_free_symtab(lily_symtab *);

lily_literal *lily_get_integer_literal(lily_symtab *, int64_t);
lily_literal *lily_get_double_literal(lily_symtab *, double);
lily_literal *lily_get_string_literal(lily_symtab *, char *);

lily_class *lily_class_by_id(lily_symtab *, int);
lily_class *lily_class_by_name(lily_symtab *, const char *);
lily_var *lily_find_class_callable(lily_symtab *, lily_class *, char *);
const lily_func_seed *lily_find_class_call_seed(lily_symtab *, lily_class *,
        char *);
const lily_func_seed *lily_get_global_seed_chain();
lily_prop_entry *lily_find_property(lily_symtab *, lily_class *, char *);

lily_var *lily_try_new_var(lily_symtab *, lily_sig *, char *, int);

lily_var *lily_scoped_var_by_name(lily_symtab *, lily_var *, char *);
lily_var *lily_var_by_name(lily_symtab *, char *);

int lily_keyword_by_name(char *);

lily_sig *lily_try_sig_for_class(lily_symtab *, lily_class *);
lily_sig *lily_try_sig_from_ids(lily_symtab *, const int *);
lily_sig *lily_build_ensure_sig(lily_symtab *, lily_class *, int, lily_sig **, int, int);
void lily_update_enum_class(lily_symtab *, lily_class *, lily_sig **, int);

void lily_hide_block_vars(lily_symtab *, lily_var *);
int lily_check_right_inherits_or_is(lily_class *, lily_class *);

lily_class *lily_new_class(lily_symtab *, char *);
lily_prop_entry *lily_add_class_property(lily_class *, lily_sig *, char *, int);
void lily_update_symtab_generics(lily_symtab *, lily_class *, int);
void lily_finish_class(lily_symtab *, lily_class *);
void lily_make_constructor_return_sig(lily_symtab *);
#endif
