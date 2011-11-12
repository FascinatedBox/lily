#ifndef LILY_ERROR_H
# define LILY_ERROR_H

# include <setjmp.h>

typedef struct {
    jmp_buf jump;
    char *message;
} lily_excep_data;

void lily_raise(lily_excep_data *, char *, ...);
void lily_raise_nomem(lily_excep_data *);

#endif
