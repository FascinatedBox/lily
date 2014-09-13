#ifndef LILY_SYMTAB_H
# define LILY_SYMTAB_H

# include "lily_syminfo.h"
# include "lily_raiser.h"

typedef struct {
    /* The first symbol in the table (for itering from). This is also __main__,
       since __main__ is the first symbol. */
    lily_var *var_start;
    lily_var *var_top;

    /* When a function has functions declared inside of it, those functions
       fall out of scope when the other function goes out of scope. The inner
       functions end up going in here. This makes the inner functions no longer
       reachable, but keeps them alive (for obvious reasons)!
       Emitter adds to this as needed. Symtab is only responsible for ensuring
       that it's destroyed properly at exit. */
    lily_var *old_function_chain;

    lily_class **classes;
    int class_pos;
    int class_size;
    lily_sig *root_sig;

    int function_depth;

    /* __main__ is kept by itself because it doesn't get loaded into any
       register. This keeps the vm from seeing it and thinking __main__ needs a
       ref. */
    lily_var *lily_main;

    lily_literal *lit_start;
    lily_literal *lit_top;
    int next_register_spot;
    int *lex_linenum;
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
#define KEY_SHOW        7
#define KEY__LINE__     8
#define KEY__FILE__     9
#define KEY__FUNCTION__ 10
#define KEY_FOR         11
#define KEY_DO          12
#define KEY_ISNIL       13
#define KEY_TRY         14
#define KEY_EXCEPT      15
#define KEY_RAISE       16
#define KEY_LAST_ID     16

lily_symtab *lily_new_symtab(lily_raiser *);
void lily_free_symtab_lits_and_vars(lily_symtab *);
void lily_free_symtab(lily_symtab *);

lily_literal *lily_get_integer_literal(lily_symtab *, int64_t);
lily_literal *lily_get_double_literal(lily_symtab *, double);
lily_literal *lily_get_string_literal(lily_symtab *, char *);

lily_class *lily_class_by_id(lily_symtab *, int);
lily_class *lily_class_by_name(lily_symtab *, const char *);
lily_var *lily_find_class_callable(lily_symtab *, lily_class *, char *);

lily_var *lily_try_new_var(lily_symtab *, lily_sig *, char *, int);

lily_var *lily_scoped_var_by_name(lily_symtab *, lily_var *, char *);
lily_var *lily_var_by_name(lily_symtab *, char *);

int lily_keyword_by_name(char *);

lily_sig *lily_try_sig_for_class(lily_symtab *, lily_class *);
lily_sig *lily_try_sig_from_ids(lily_symtab *, const int *);
lily_sig *lily_build_ensure_sig(lily_symtab *, lily_class *, int, lily_sig **, int, int);

void lily_hide_block_vars(lily_symtab *, lily_var *);
int lily_check_right_inherits_or_is(lily_class *, lily_class *);

#endif
