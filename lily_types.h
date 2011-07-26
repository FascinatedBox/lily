#ifndef LILY_TYPES_H
# define LILY_TYPES_H

typedef struct lily_symbol_ {
    struct lily_symbol_ *next;
    char *sym_name;
    int callable;
    int num_args;
    int *code;
    int code_len;
    int code_pos;
} lily_symbol;

#endif
