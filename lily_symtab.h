#ifndef LILY_SYMTAB_H
# define LILY_SYMTAB_H

# include "lily_syminfo.h"
# include "lily_raiser.h"
# include "lily_opcode.h"
# include "lily_syminfo.h"

typedef struct {
    /* The first symbol in the table (for itering from). This is also @main,
       since @main is the first symbol. */
    lily_var *var_start;
    lily_var *var_top;

    /* Methods declared inside of other methods are inserted into @main's
       registers. This was seen as the sanest approach (instead of making them
       literals or some special new type).
       When the outer method leaves, the inner method is added to this list.
       This is important for keeping track of signature information. */
    lily_var *old_method_chain;

    int scope;

    lily_class **classes;
    int class_pos;
    int class_size;
    lily_sig *root_sig;

    int method_depth;

    /* @main is kept by itself because it doesn't get loaded into any register.
       This keeps the vm from seeing it and thinking @main needs a ref. */
    lily_var *at_main;

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
lily_var *lily_find_class_callable(lily_class *, char *, uint64_t);
void lily_free_symtab(lily_symtab *);
int lily_keyword_by_name(char *, uint64_t);
lily_literal *lily_new_literal(lily_symtab *, lily_class *, lily_value);
lily_literal *lily_get_intnum_literal(lily_symtab *, lily_class *, lily_value);
lily_literal *lily_get_str_literal(lily_symtab *, char *);
lily_symtab *lily_new_symtab(lily_raiser *);
lily_var *lily_try_new_var(lily_symtab *, lily_sig *, char *, uint64_t);
lily_var *lily_var_by_name(lily_symtab *, char *, uint64_t);
lily_function_val *lily_try_new_function_val(lily_func, char *);
lily_method_val *lily_try_new_method_val();
lily_object_val *lily_try_new_object_val();
lily_sig *lily_try_sig_for_class(lily_symtab *, lily_class *);
void lily_deref_method_val(lily_method_val *);
void lily_deref_str_val(lily_str_val *);
void lily_deref_object_val(lily_object_val *);
void lily_deref_list_val_by(lily_sig *, lily_list_val *, int);
void lily_deref_list_val(lily_sig *, lily_list_val *);
void lily_deref_unknown_val(lily_sig *, lily_value);
void lily_hide_block_vars(lily_symtab *, lily_var *);
void lily_add_sig_to_msgbuf(lily_msgbuf *, lily_sig *);
void lily_save_declared_method(lily_symtab *, lily_var *);
lily_sig *lily_ensure_unique_sig(lily_symtab *, lily_sig *);

#endif
