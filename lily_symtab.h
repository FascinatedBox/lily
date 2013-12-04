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
    /* The last symbol (for adding to). */
    lily_var *var_top;
    lily_var *old_var_start;
    lily_var *old_var_top;
    lily_class **classes;
    int class_pos;
    int class_size;
    lily_sig *root_sig;

    lily_literal *lit_start;
    lily_literal *lit_top;
    int next_var_id;
    int next_lit_id;
    int next_storage_id;
    int *lex_linenum;
    lily_raiser *raiser;
} lily_symtab;

/* Sync with keywords in lily_seed_symtab.h. */
#define KEY_IF       0
#define KEY_ELIF     1
#define KEY_ELSE     2
#define KEY_RETURN   3
#define KEY_WHILE    4
#define KEY_CONTINUE 5
#define KEY_BREAK    6
#define KEY_SHOW     7
#define KEY__LINE__  8
#define KEY__FILE__  9
#define KEY_LAST_ID  9

lily_class *lily_class_by_id(lily_symtab *, int);
lily_class *lily_class_by_name(lily_symtab *, char *);
lily_var *lily_find_class_callable(lily_class *, char *);
void lily_free_symtab(lily_symtab *);
int lily_keyword_by_name(char *);
lily_literal *lily_new_literal(lily_symtab *, lily_class *, lily_value);
lily_literal *lily_get_line_literal(lily_symtab *);
lily_literal *lily_get_file_literal(lily_symtab *, char *);
lily_symtab *lily_new_symtab(lily_raiser *);
lily_var *lily_try_new_var(lily_symtab *, lily_sig *, char *);
lily_var *lily_var_by_name(lily_symtab *, char *);
int lily_try_add_sig_storage(lily_symtab *, lily_sig *);
int lily_try_add_storage(lily_symtab *, lily_sig *);
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
void lily_drop_block_vars(lily_symtab *, lily_var *);
int lily_sigequal(lily_sig *, lily_sig *);
void lily_add_sig_to_msgbuf(lily_msgbuf *, lily_sig *);

#endif
