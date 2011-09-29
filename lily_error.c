#include <stdarg.h>

#include "lily_impl.h"
#include "lily_error.h"

void lily_raise_nomem(lily_excep_data *error)
{
    error->code = err_nomem;
    longjmp(error->jump, 1);
}

void lily_raise(lily_excep_data *error, lily_excep_code code, char *fmt, ...)
{
    /* A best effort at making sure interp->excep_msg is the whole error
       message. err_msg set to NULL if that's not possible. */
    char *buffer, *tmpbuffer;
    int i, va_size;
    va_list arglist;
    size_t cursize, nextsize;

    cursize = 0;
    while (1) {
        if (cursize == 0) {
            buffer = lily_malloc(64 * sizeof(char));
            if (buffer == NULL) {
                error->message = NULL;
                break;
            }
            cursize = 64;
        }
        else if (tmpbuffer = realloc(buffer, nextsize)) {
            buffer = tmpbuffer;
            nextsize = cursize;
        }
        else {
            free(buffer);
            error->message = NULL;
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
            error->message = buffer;
            break;
        }
    }

    error->code = code;
    longjmp(error->jump, 1);
}
