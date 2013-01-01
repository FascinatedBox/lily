#ifndef LILY_ERROR_H
# define LILY_ERROR_H

# include <setjmp.h>

# include "lily_msgbuf.h"

# define lily_ErrNoMemory 0
# define lily_ErrSyntax   1
# define lily_ErrImport   2
# define lily_ErrEncoding 3
# define lily_ErrNoValue  4

typedef struct {
    jmp_buf jump;
    int error_code;
    char *message;
    /* This is 0 if the error line is the lexer's current line number.
       Otherwise, this is the line number to report. It is not an offset.
       Uses:
       * Merging ASTs. number a = 1 +
         1 should report an error on line 1 (the assignment), not line 2.
       * Any vm error. */
    int line_adjust;
} lily_excep_data;

void lily_raise(lily_excep_data *, int, char *, ...);
void lily_raise_msgbuf(lily_excep_data *, int, lily_msgbuf *);
void lily_raise_nomem(lily_excep_data *);
const char *lily_name_for_error(int);

#endif
