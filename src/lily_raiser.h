#ifndef LILY_RAISER_H
# define LILY_RAISER_H

# include <setjmp.h>

# include "lily_syminfo.h"
# include "lily_msgbuf.h"

# define lily_NoMemoryError        0
# define lily_SyntaxError          1
# define lily_ImportError          2
# define lily_EncodingError        3
# define lily_DivisionByZeroError  4
# define lily_IndexError           5
# define lily_BadTypecastError     6
# define lily_ReturnExpectedError  7
# define lily_ValueError           8
# define lily_RecursionError       9
# define lily_KeyError            10
# define lily_FormatError         11

typedef struct {
    /* The raiser will typically have two jumps: One for the vm to catch runtime
       errors, and a second for the runner to catch parser errors. The raiser
       will use the highest jump it has (the vm, typically). */
    jmp_buf *jumps;
    int jump_pos;
    int jump_size;

    lily_value *exception;
    int error_code;
    lily_msgbuf *msgbuf;
    /* This is 0 if the error line is the lexer's current line number.
       Otherwise, this is the line number to report. It is not an offset.
       Uses:
       * Merging ASTs. number a = 1 +
         1 should report an error on line 1 (the assignment), not line 2.
       * Any vm error. */
    int line_adjust;
} lily_raiser;

lily_raiser *lily_new_raiser(void);
void lily_free_raiser(lily_raiser *);
void lily_raise(lily_raiser *, int, char *, ...);
void lily_raise_nomem(lily_raiser *);
void lily_raise_prebuilt(lily_raiser *, int);
void lily_raise_value(lily_raiser *, lily_value *);

const char *lily_name_for_error(int);

#endif
