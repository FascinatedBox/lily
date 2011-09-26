#include <stdarg.h>

#include "lily_impl.h"
#include "lily_interp.h"

#define EXCEP_NONE   0
#define EXCEP_NOMEM  1
#define EXCEP_SYNTAX 2

lily_interp *lily_init_interp(void)
{
    lily_interp *interp = lily_malloc(sizeof(lily_interp));
    if (interp == NULL)
        return NULL;

    if (setjmp(interp->excep_jmp) == 0) {
        lily_init_symtab(interp);
        lily_init_lexer(interp);
    }

    return interp;
}

int lily_parse_file(lily_interp *interp, char *filename)
{
    if (setjmp(interp->excep_jmp) == 0) {
        lily_include(interp, filename);
        lily_parser(interp);
        return 1;
    }

    return 0;
}

void lily_raise_nomem(lily_interp *interp)
{
    interp->excep_code = err_nomem;
    longjmp(interp->excep_jmp, 1);
}

void lily_raise(lily_interp *interp, lily_excep_code code, char *fmt, ...)
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
                interp->excep_msg = NULL;
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
            interp->excep_msg = NULL;
            break;
        }

        va_start(arglist, fmt);
        va_size = vsnprintf(buffer, cursize, fmt, arglist);
        fprintf(stderr, "va_size is %d, cursize is %d.\n", va_size, cursize);
        if (va_size == -1 ||
            va_size == cursize ||
            va_size == cursize - 1)
            nextsize = va_size * 2;
        else if (va_size > cursize)
            nextsize = va_size + 2;
        else {
            interp->excep_msg = buffer;
            break;
        }
    }

    interp->excep_code = code;
    longjmp(interp->excep_jmp, 1);
}
