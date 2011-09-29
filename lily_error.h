#ifndef LILY_ERROR_H
# define LILY_ERROR_H

# include <setjmp.h>

typedef enum {
    err_include,
    err_internal,
    err_nomem,
    err_stub,
    err_syntax
} lily_excep_code;

typedef struct {
    jmp_buf jump;
    lily_excep_code code;
    char *message;
} lily_excep_data;

void lily_raise(lily_excep_data *, lily_excep_code, char *, ...);
void lily_raise_nomem(lily_excep_data *);

#endif
