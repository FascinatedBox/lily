#ifndef LILY_SYMTAB_H
# define LILY_SYMTAB_H

typedef enum {
    vt_builtin,
    vt_str
} lily_val_type;

typedef struct {
    char *str;
    int str_size;
} lily_strval;

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

lily_symbol *symtab;
lily_symbol *main_func;

lily_symbol *lily_st_new_str_sym(char *);
lily_symbol *lily_st_find_symbol(char *);
void lily_init_symtab(void);

#endif
