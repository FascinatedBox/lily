#ifndef LILY_RAISER_H
# define LILY_RAISER_H

# include <setjmp.h>

# include "lily_msgbuf.h"

# define lily_ErrNoMemory     0
# define lily_ErrSyntax       1
# define lily_ErrImport       2
# define lily_ErrEncoding     3
# define lily_ErrNoValue      4
# define lily_ErrDivideByZero 5
# define lily_ErrOutOfRange   6
# define lily_ErrBadCast      7

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
} lily_raiser;

void lily_raise(lily_raiser *, int, char *, ...);
void lily_raise_msgbuf(lily_raiser *, int, lily_msgbuf *);
void lily_raise_nomem(lily_raiser *);
const char *lily_name_for_error(int);

#endif
