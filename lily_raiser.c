#include <stdarg.h>

#include "lily_impl.h"
#include "lily_raiser.h"

static const char *lily_error_names[] =
    {"ErrNoMemory", "ErrSyntax", "ErrImport", "ErrEncoding", "ErrNoValue"};

void lily_raise(lily_raiser *raiser, int error_code, char *fmt, ...)
{
    /* A best effort at making sure raiser->message is the whole error message.
       message set to NULL if that's not possible. */
    char *buffer, *tmpbuffer;
    int va_size;
    va_list arglist;
    size_t cursize, nextsize;

    cursize = 0;
    while (1) {
        if (cursize == 0) {
            buffer = lily_malloc(64 * sizeof(char));
            if (buffer == NULL) {
                raiser->message = NULL;
                break;
            }
            cursize = 64;
        }
        else if ((tmpbuffer = lily_realloc(buffer, nextsize))) {
            buffer = tmpbuffer;
            nextsize = cursize;
        }
        else {
            lily_free(buffer);
            raiser->message = NULL;
            break;
        }

        va_start(arglist, fmt);
        va_size = vsnprintf(buffer, cursize, fmt, arglist);

        if (va_size == -1 ||
            va_size == cursize ||
            va_size == cursize - 1)
            nextsize = va_size * 2;
        else if (va_size > cursize)
            nextsize = va_size + 2;
        else {
            raiser->message = buffer;
            break;
        }
    }

    raiser->error_code = error_code;
    longjmp(raiser->jump, 1);
}

void lily_raise_msgbuf(lily_raiser *raiser, int error_code, lily_msgbuf *mb)
{
    raiser->error_code = error_code;
    raiser->message = mb->msg;
    lily_free(mb);

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
