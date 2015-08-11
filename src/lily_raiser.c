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

lily_raiser *lily_new_raiser(void)
{
    lily_raiser *raiser = lily_malloc(sizeof(lily_raiser));
    lily_jump_link *first_jump = malloc(sizeof(lily_jump_link));
    first_jump->prev = NULL;
    first_jump->next = NULL;

    raiser->msgbuf = lily_new_msgbuf();
    raiser->all_jumps = first_jump;
    raiser->line_adjust = 0;
    raiser->exception_type = NULL;
    raiser->exception_value = NULL;

    return raiser;
}

void lily_free_raiser(lily_raiser *raiser)
{
    if (raiser->msgbuf)
        lily_free_msgbuf(raiser->msgbuf);

    lily_jump_link *jump_next;
    while (raiser->all_jumps->prev)
        raiser->all_jumps = raiser->all_jumps->prev;

    while (raiser->all_jumps) {
        jump_next = raiser->all_jumps->next;
        lily_free(raiser->all_jumps);
        raiser->all_jumps = jump_next;
    }

    lily_free(raiser);
}

/* This will either allocate a new slot for a jump, or yield one that is
   currently not being used. */
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

/* This is used by vm as a means of restoring control to the previously held
   jump. */
void lily_jump_back(lily_raiser *raiser)
{
    raiser->all_jumps = raiser->all_jumps->prev;
    longjmp(raiser->all_jumps->jump, 1);
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
    longjmp(raiser->all_jumps->jump, 1);
}

/* lily_raise_prebuilt
   This is similar to lily_raise, except that the raiser's msgbuf has already
   been prepared with the proper error message. */
void lily_raise_prebuilt(lily_raiser *raiser, int error_code)
{
    raiser->error_code = error_code;
    longjmp(raiser->all_jumps->jump, 1);
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
    longjmp(raiser->all_jumps->jump, 1);
}

void lily_raise_type_and_msg(lily_raiser *raiser, lily_type *type,
        const char *msg)
{
    raiser->exception_type = type;
    lily_msgbuf_flush(raiser->msgbuf);
    lily_msgbuf_add(raiser->msgbuf, msg);

    longjmp(raiser->all_jumps->jump, 1);
}

void lily_raise_value(lily_raiser *raiser, lily_value *v, const char *msg)
{
    raiser->exception_value = v;
    raiser->exception_type = v->type;
    lily_msgbuf_flush(raiser->msgbuf);
    lily_msgbuf_add(raiser->msgbuf, msg);

    longjmp(raiser->all_jumps->jump, 1);
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
