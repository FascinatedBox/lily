#ifndef LILY_TYPES_H
# define LILY_TYPES_H

typedef enum {
    vt_builtin,
    vt_str
} lily_val_type;

typedef struct {
    char *str;
    int str_size;
} lily_strval;

typedef struct lily_symbol_ {
    struct lily_symbol_ *next;
    char *sym_name;
    int callable;
    int num_args;
    int *code;
    int code_len;
    int code_pos;
    void *sym_value;
    lily_val_type val_type;
} lily_symbol;

#endif
