#ifndef LILY_SYMTAB_H
# define LILY_SYMTAB_H

# include "lily_syminfo.h"
# include "lily_raiser.h"

typedef struct {
    /* The first symbol in the table (for itering from). This is also __main__,
       since __main__ is the first symbol. */
    lily_var *var_start;
    lily_var *var_top;

    /* When a method has methods declared inside of it, those methods fall out
       of scope when the other method goes out of scope. The inner methods end
       up going in here. This makes the inner methods no longer reachable, but
       keeps them alive (for obvious reasons)!
       Emitter adds to this as needed. Symtab is only responsible for ensuring
       that it's destroyed properly at exit. */
    lily_var *old_method_chain;

    lily_class **classes;
    int class_pos;
    int class_size;
    lily_sig *root_sig;

    int method_depth;

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
#define KEY_IF        0
#define KEY_ELIF      1
#define KEY_ELSE      2
#define KEY_RETURN    3
#define KEY_WHILE     4
#define KEY_CONTINUE  5
#define KEY_BREAK     6
#define KEY_SHOW      7
#define KEY__LINE__   8
#define KEY__FILE__   9
#define KEY__METHOD__ 10
#define KEY_FOR       11
#define KEY_DO        12
#define KEY_LAST_ID   12

lily_class *lily_class_by_id(lily_symtab *, int);
lily_class *lily_class_by_hash(lily_symtab *, uint64_t);
lily_var *lily_find_class_callable(lily_symtab *, lily_class *, char *,
        uint64_t);

void lily_free_symtab_lits_and_vars(lily_symtab *);
void lily_free_symtab(lily_symtab *);
int lily_keyword_by_name(char *, uint64_t);
lily_literal *lily_new_literal(lily_symtab *, lily_class *, lily_raw_value);
lily_literal *lily_get_intnum_literal(lily_symtab *, lily_class *,
        lily_raw_value);

lily_literal *lily_get_str_literal(lily_symtab *, char *);
lily_symtab *lily_new_symtab(lily_raiser *);
lily_var *lily_try_new_var(lily_symtab *, lily_sig *, char *, uint64_t, int);
lily_var *lily_var_by_name(lily_symtab *, char *, uint64_t);
lily_sig *lily_try_sig_for_class(lily_symtab *, lily_class *);
void lily_hide_block_vars(lily_symtab *, lily_var *);
lily_sig *lily_ensure_unique_sig(lily_symtab *, lily_sig *);

#endif
