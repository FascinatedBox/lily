#include <stdarg.h>
#include <string.h>

#include "lily_impl.h"
#include "lily_raiser.h"

/* This is used by lily_name_for_error to get a printable name for an error
   code. This is used by lily_fs to show what kind of error occured. */
static const char *lily_error_names[] =
    {"NoMemoryError", "SyntaxError", "ImportError", "EncodingError",
     "DivisionByZeroError", "IndexError", "BadTypecastError", "NoReturnError",
     "ValueError", "RecursionError", "KeyError", "FormatError"};

lily_raiser *lily_new_raiser()
{
    lily_raiser *raiser = lily_malloc(sizeof(lily_raiser));
    if (raiser == NULL)
        return NULL;

    raiser->msgbuf = lily_new_msgbuf();
    raiser->jumps = lily_malloc(2 * sizeof(jmp_buf));
    raiser->jump_pos = 0;
    raiser->jump_size = 2;
    raiser->line_adjust = 0;
    raiser->exception = NULL;

    if (raiser->msgbuf == NULL || raiser->jumps == NULL) {
        lily_free_raiser(raiser);
        return NULL;
    }

    return raiser;
}

void lily_free_raiser(lily_raiser *raiser)
{
    if (raiser->msgbuf)
        lily_free_msgbuf(raiser->msgbuf);

    lily_free(raiser->jumps);
    lily_free(raiser);
}

/* lily_raise
   This stops the interpreter. error_code is one of the error codes defined in
   lily_raiser.h, which are matched to lily_error_names. Every error passes
   through here except NoMemoryError.
   Instead of printing the message, this function saves the message so that
   whatever runs the interpreter can choose what to do with it (ignoring it,
   printing it to a special file, printing it to an application window, etc.) */
void lily_raise(lily_raiser *raiser, int error_code, char *fmt, ...)
{
    /* This error message is more important than whatever's in here. So blast
       the buffer (and maybe any truncation flag). */
    lily_msgbuf_reset(raiser->msgbuf);

    va_list var_args;
    va_start(var_args, fmt);
    lily_msgbuf_add_fmt_va(raiser->msgbuf, fmt, var_args);
    va_end(var_args);

    raiser->error_code = error_code;
    longjmp(raiser->jumps[raiser->jump_pos-1], 1);
}

void lily_raise_nomem(lily_raiser *raiser)
{
    raiser->error_code = lily_NoMemoryError;
    longjmp(raiser->jumps[raiser->jump_pos-1], 1);
}

/* lily_raise_prebuilt
   This is similar to lily_raise, except that the raiser's msgbuf has already
   been prepared with the proper error message. */
void lily_raise_prebuilt(lily_raiser *raiser, int error_code)
{
    raiser->error_code = error_code;
    longjmp(raiser->jumps[raiser->jump_pos-1], 1);
}

void lily_raise_value(lily_raiser *raiser, lily_value *value)
{
    raiser->exception = value;
    longjmp(raiser->jumps[raiser->jump_pos-1], 1);
}

const char *lily_name_for_error(int error_code)
{
    return lily_error_names[error_code];
}
