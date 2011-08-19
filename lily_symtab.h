#ifndef LILY_SYMTAB_H
# define LILY_SYMTAB_H

typedef enum {
    vt_builtin,
    vt_int,
    vt_str,
    vt_list,
    vt_float,
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

typedef struct lily_symbol_ {
    struct lily_symbol_ *next;
    char *sym_name;
    int sym_id;
    int callable;
    int num_args;
    void *sym_value;
    lily_code_data *code_data;
    lily_val_type val_type;
} lily_symbol;

/* Keep this synced with the keyword table in lily_symtab.c */
#define SYM_ID_STR   0
#define SYM_ID_PRINT 1

lily_symbol *symtab;
lily_symbol *main_func;

lily_symbol *lily_st_new_var_sym(char *);
lily_symbol *lily_st_new_str_sym(char *);
lily_symbol *lily_st_new_int_sym(int);
lily_symbol *lily_st_find_symbol(char *);
void lily_init_symtab(void);

#endif
