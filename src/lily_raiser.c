#include <stdarg.h>
#include <string.h>

#include "lily_alloc.h"
#include "lily_raiser.h"
#include "lily_seed.h"

/* This is used by lily_name_for_error to get a printable name for an error
   code. This is used by lily_fs to show what kind of error occured. */
static const char *lily_error_names[] =
    {"Error", "SyntaxError", "DivisionByZeroError", "IndexError",
     "BadTypecastError", "ValueError", "RecursionError", "KeyError",
     "FormatError", "IOError"};


lily_raiser *lily_new_raiser(lily_options *options)
{
    lily_raiser *raiser = lily_malloc(sizeof(lily_raiser));

    raiser->msgbuf = lily_new_msgbuf(options);
    raiser->jumps = lily_malloc(2 * sizeof(jmp_buf));
    raiser->jump_pos = 0;
    raiser->jump_size = 2;
    raiser->line_adjust = 0;
    raiser->exception_type = NULL;

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
   through here.
   Instead of printing the message, this function saves the message so that
   whatever runs the interpreter can choose what to do with it (ignoring it,
   printing it to a special file, printing it to an application window, etc.) */
void lily_raise(lily_raiser *raiser, int error_code, char *fmt, ...)
{
    /* This is more important than whatever the msgbuf currently has. Blast the
       current contents away. */
    lily_msgbuf_flush(raiser->msgbuf);
    /* Clear out any value raised previously, since otherwise
       lily_name_for_error will grab the name using that. */
    raiser->exception_type = NULL;

    va_list var_args;
    va_start(var_args, fmt);
    lily_msgbuf_add_fmt_va(raiser->msgbuf, fmt, var_args);
    va_end(var_args);

    raiser->exception_type = NULL;
    raiser->error_code = error_code;
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

/*  This is called by the vm to set a type and a message on the raiser, on
    behalf of a loaded module. This function intentionally does not do the
    longjmp, in case the module needs to do some cleanup. */
void lily_raiser_set_error(lily_raiser *raiser, lily_type *type,
        const char *msg)
{
    raiser->exception_type = type;
    lily_msgbuf_flush(raiser->msgbuf);
    lily_msgbuf_add(raiser->msgbuf, msg);
}

void lily_raise_prepared(lily_raiser *raiser)
{
    longjmp(raiser->jumps[raiser->jump_pos-1], 1);
}

void lily_raise_type_and_msg(lily_raiser *raiser, lily_type *type,
        const char *msg)
{
    raiser->exception_type = type;
    lily_msgbuf_flush(raiser->msgbuf);
    lily_msgbuf_add(raiser->msgbuf, msg);

    longjmp(raiser->jumps[raiser->jump_pos-1], 1);
}

const char *lily_name_for_error(lily_raiser *raiser)
{
    const char *result;

    if (raiser->exception_type)
        result = raiser->exception_type->cls->name;
    else
        result = lily_error_names[raiser->error_code];

    return result;
}
