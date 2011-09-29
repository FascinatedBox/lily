#ifndef LILY_SYMTAB_H
# define LILY_SYMTAB_H

# include "lily_error.h"

typedef enum {
    vt_builtin,
    vt_int,
    vt_str,
    vt_list,
    vt_double,
    vt_unknown,
} lily_val_type;

/* There's no struct for storing floats or ints. They're stored raw (just cast
   the pointer). */

typedef struct {
    char *str;
    int str_size;
} lily_strval;

typedef struct {
    void *values;
    lily_val_type *val_types;
    int val_count;
    int val_size;
} lily_listval;

typedef struct {
    int *code;
    int code_len;
    int code_pos;
} lily_code_data;

typedef struct lily_symbol_t {
    struct lily_symbol_t *next;
    char *sym_name;
    int sym_id;
    int line_num;
    int callable;
    int num_args;
    void *sym_value;
    lily_code_data *code_data;
    lily_val_type val_type;
} lily_symbol;

typedef struct {
    /* The first symbol in the table (for itering from). */
    lily_symbol *start;
    /* The last symbol (for adding to). */
    lily_symbol *top;
    /* The function containing commands outside of functions. */
    lily_symbol *main;
    int new_sym_id;
    int *lex_linenum;
    lily_excep_data *error;
} lily_symtab;

/* Keep this synced with the keyword table in lily_symtab.c */
#define SYM_ID_STR   0
#define SYM_ID_PRINT 1

lily_symtab *lily_new_symtab(lily_excep_data *);
lily_symbol *lily_st_new_var_sym(lily_symtab *, char *);
lily_symbol *lily_st_new_str_sym(lily_symtab *, char *);
lily_symbol *lily_st_new_int_sym(lily_symtab *, int);
lily_symbol *lily_st_new_dbl_sym(lily_symtab *, double);
lily_symbol *lily_st_find_symbol(lily_symtab *, char *);

#endif
