#include <stdarg.h>
#include <string.h>

#include "lily_raiser.h"

#include "lily_api_alloc.h"

/* If the error raised is a code (and not a proper error), then this is used to
   get a name for the error. */
static const char *lily_error_names[] =
    {"Error", "SyntaxError", "DivisionByZeroError", "IndexError",
     "ValueError", "RuntimeError", "KeyError", "IOError"};

lily_raiser *lily_new_raiser(void)
{
    lily_raiser *raiser = lily_malloc(sizeof(lily_raiser));
    lily_jump_link *first_jump = lily_malloc(sizeof(lily_jump_link));
    first_jump->prev = NULL;
    first_jump->next = NULL;

    raiser->msgbuf = lily_new_msgbuf();
    raiser->aux_msgbuf = lily_new_msgbuf();
    raiser->all_jumps = first_jump;
    raiser->line_adjust = 0;
    raiser->exception_cls = NULL;

    return raiser;
}

void lily_free_raiser(lily_raiser *raiser)
{
    lily_jump_link *jump_next;
    while (raiser->all_jumps->prev)
        raiser->all_jumps = raiser->all_jumps->prev;

    while (raiser->all_jumps) {
        jump_next = raiser->all_jumps->next;
        lily_free(raiser->all_jumps);
        raiser->all_jumps = jump_next;
    }

    lily_free_msgbuf(raiser->aux_msgbuf);
    lily_free_msgbuf(raiser->msgbuf);
    lily_free(raiser);
}

/* This ensures that there is space for a jump for the caller. It will first try
   to reuse a jump, then to allocate a new one. */
lily_jump_link *lily_jump_setup(lily_raiser *raiser)
{
    if (raiser->all_jumps->next)
        raiser->all_jumps = raiser->all_jumps->next;
    else {
        lily_jump_link *new_link = lily_malloc(sizeof(lily_jump_link));
        new_link->prev = raiser->all_jumps;
        raiser->all_jumps->next = new_link;

        new_link->next = NULL;

        raiser->all_jumps = new_link;
    }

    return raiser->all_jumps;
}

/* Releases the jump provided by lily_jump_setup. */
void lily_release_jump(lily_raiser *raiser)
{
    raiser->all_jumps = raiser->all_jumps->prev;
}

/* This will execute a jump to the most recently-held jump. This should only be
   called if an error has been set. */
void lily_jump_back(lily_raiser *raiser)
{
    raiser->all_jumps = raiser->all_jumps->prev;
    longjmp(raiser->all_jumps->jump, 1);
}

/* This raises an error for the given 'error_code'. The vm will need to load a
   proper exception for the code (or the raiser will die). */
void lily_raise(lily_raiser *raiser, int error_code, const char *fmt, ...)
{
    lily_mb_flush(raiser->msgbuf);
    raiser->exception_cls = NULL;

    va_list var_args;
    va_start(var_args, fmt);
    lily_mb_add_fmt_va(raiser->msgbuf, fmt, var_args);
    va_end(var_args);

    raiser->error_code = error_code;
    longjmp(raiser->all_jumps->jump, 1);
}

/* This is called by the vm to raise an exception via a class and a message. If
   the exception is a proper Lily value, then the vm will hold that value. */
void lily_raise_class(lily_raiser *raiser, lily_class *raise_cls,
        const char *msg)
{
    raiser->exception_cls = raise_cls;
    lily_mb_flush(raiser->msgbuf);
    lily_mb_add(raiser->msgbuf, msg);

    longjmp(raiser->all_jumps->jump, 1);
}

/* This fetches the name of the currently-registered exception. */
const char *lily_name_for_error(lily_raiser *raiser)
{
    const char *result;

    if (raiser->exception_cls)
        result = raiser->exception_cls->name;
    else
        result = lily_error_names[raiser->error_code];

    return result;
}
