#include <stdarg.h>
#include <string.h>

#include "lily_impl.h"
#include "lily_raiser.h"

/* This is used by lily_name_for_error to get a printable name for an error
   code. This is used by lily_fs to show what kind of error occured. */
static const char *lily_error_names[] =
    {"ErrNoMemory", "ErrSyntax", "ErrImport", "ErrEncoding", "ErrNoValue",
     "ErrDivideByZero", "ErrOutOfRange", "ErrBadCast"};

lily_raiser *lily_new_raiser()
{
    lily_raiser *raiser = lily_malloc(sizeof(lily_raiser));
    lily_msgbuf *msgbuf = lily_new_msgbuf();

    if (msgbuf == NULL || raiser == NULL) {
        if (msgbuf != NULL)
            lily_free_msgbuf(msgbuf);
        else if (raiser != NULL)
            lily_free(raiser);

        return NULL;
    }

    raiser->line_adjust = 0;
    raiser->msgbuf = msgbuf;
    return raiser;
}

void lily_free_raiser(lily_raiser *raiser)
{
    lily_free_msgbuf(raiser->msgbuf);
    lily_free(raiser);
}

/* lily_raise
   This stops the interpreter. error_code is one of the error codes defined in
   lily_raiser.h, which are matched to lily_error_names. Every error passes
   through here except ErrNoMemory.
   Instead of printing the message, this function saves the message so that
   whatever runs the interpreter can choose what to do with it (ignoring it,
   printing it to a special file, printing it to an application window, etc.) */
void lily_raise(lily_raiser *raiser, int error_code, char *fmt, ...)
{
    int i, len, text_start;
    va_list var_args;

    va_start(var_args, fmt);

    text_start = 0;
    len = strlen(fmt);

    for (i = 0;i < len;i++) {
        char c = fmt[i];
        if (c == '%') {
            if (i + 1 == len)
                break;

            if (i != text_start)
                lily_msgbuf_add_text_range(raiser->msgbuf, fmt, text_start, i);

            i++;
            c = fmt[i];
            if (c == 's') {
                char *str = va_arg(var_args, char *);
                lily_msgbuf_add(raiser->msgbuf, str);
            }
            else if (c == 'd') {
                int d = va_arg(var_args, int);
                lily_msgbuf_add_int(raiser->msgbuf, d);
            }
            else if (c == 'T') {
                lily_sig *sig = va_arg(var_args, lily_sig *);
                lily_msgbuf_add_sig(raiser->msgbuf, sig);
            }

            text_start = i+1;
        }
    }

    if (i != text_start)
        lily_msgbuf_add_text_range(raiser->msgbuf, fmt, text_start, i);

    raiser->error_code = error_code;
    longjmp(raiser->jump, 1);
}

void lily_raise_nomem(lily_raiser *raiser)
{
    raiser->error_code = lily_ErrNoMemory;
    longjmp(raiser->jump, 1);
}

const char *lily_name_for_error(int error_code)
{
    return lily_error_names[error_code];
}
