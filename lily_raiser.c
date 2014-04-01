#include <stdarg.h>
#include <string.h>

#include "lily_impl.h"
#include "lily_raiser.h"
#include "lily_syminfo.h"

/* This is used by lily_name_for_error to get a printable name for an error
   code. This is used by lily_fs to show what kind of error occured. */
static const char *lily_error_names[] =
    {"ErrNoMemory", "ErrSyntax", "ErrImport", "ErrEncoding", "ErrNoValue",
     "ErrDivideByZero", "ErrOutOfRange", "ErrBadCast", "ErrReturnExpected",
     "ErrBadValue"};

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

/* add_sig_to_msgbuf
   This is kept out of lily_msgbuf.{c,h} so that things using the raiser don't
   have to pull even more info about other modules. Sigs are fairly complex. */
void add_sig_to_msgbuf(lily_msgbuf *msgbuf, lily_sig *sig)
{
    lily_msgbuf_add(msgbuf, sig->cls->name);

    if (sig->cls->id == SYM_CLASS_METHOD ||
        sig->cls->id == SYM_CLASS_FUNCTION) {
        lily_msgbuf_add(msgbuf, " (");
        if (sig->siglist[1] != NULL) {
            int i;

            for (i = 1;i < sig->siglist_size - 1;i++) {
                add_sig_to_msgbuf(msgbuf, sig->siglist[i]);
                lily_msgbuf_add(msgbuf, ", ");
            }

            add_sig_to_msgbuf(msgbuf, sig->siglist[i]);
            if (sig->flags & SIG_IS_VARARGS)
                lily_msgbuf_add(msgbuf, "...");
        }
        lily_msgbuf_add(msgbuf, "):");
        if (sig->siglist[0] == NULL)
            lily_msgbuf_add(msgbuf, "nil");
        else
            add_sig_to_msgbuf(msgbuf, sig->siglist[0]);
    }
    else if (sig->cls->id == SYM_CLASS_LIST ||
             sig->cls->id == SYM_CLASS_HASH) {
        int i;
        lily_msgbuf_add(msgbuf, "[");
        for (i = 0;i < sig->cls->template_count;i++) {
            add_sig_to_msgbuf(msgbuf, sig->siglist[i]);
            if (i != (sig->cls->template_count - 1))
                lily_msgbuf_add(msgbuf, ", ");
        }
        lily_msgbuf_add(msgbuf, "]");
    }
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
                add_sig_to_msgbuf(raiser->msgbuf, sig);
            }

            text_start = i+1;
        }
    }

    if (i != text_start)
        lily_msgbuf_add_text_range(raiser->msgbuf, fmt, text_start, i);

    raiser->error_code = error_code;
    longjmp(raiser->jumps[raiser->jump_pos-1], 1);
}

void lily_raise_nomem(lily_raiser *raiser)
{
    raiser->error_code = lily_ErrNoMemory;
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

const char *lily_name_for_error(int error_code)
{
    return lily_error_names[error_code];
}
