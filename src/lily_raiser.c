#include <stdarg.h>
#include <string.h>

#include "lily_impl.h"
#include "lily_raiser.h"

/* This is used by lily_name_for_error to get a printable name for an error
   code. This is used by lily_fs to show what kind of error occured. */
static const char *lily_error_names[] =
    {"Error", "SyntaxError", "DivisionByZeroError", "IndexError",
     "BadTypecastError", "ValueError", "RecursionError", "KeyError",
     "FormatError", "IOError"};

#define malloc_mem(size)             raiser->mem_func(NULL, size)
#define free_mem(ptr)          (void)raiser->mem_func(ptr, 0)

lily_raiser *lily_new_raiser(lily_mem_func mem_func)
{
    lily_raiser *raiser = mem_func(NULL, sizeof(lily_raiser));

    raiser->mem_func = mem_func;
    raiser->msgbuf = lily_new_msgbuf(mem_func);
    raiser->jumps = malloc_mem(2 * sizeof(jmp_buf));
    raiser->jump_pos = 0;
    raiser->jump_size = 2;
    raiser->line_adjust = 0;
    raiser->exception = NULL;

    return raiser;
}

void lily_free_raiser(lily_raiser *raiser)
{
    if (raiser->msgbuf)
        lily_free_msgbuf(raiser->msgbuf);

    free_mem(raiser->jumps);
    free_mem(raiser);
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
    raiser->exception = NULL;

    va_list var_args;
    va_start(var_args, fmt);
    lily_msgbuf_add_fmt_va(raiser->msgbuf, fmt, var_args);
    va_end(var_args);

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

void lily_raise_value(lily_raiser *raiser, lily_value *value)
{
    raiser->exception = value;
    /* This next part relies upon the Exception class being ordered as
       traceback, then message. */
    lily_instance_val *iv = value->value.instance;
    char *message = iv->values[0]->value.string->string;
    lily_msgbuf_flush(raiser->msgbuf);
    lily_msgbuf_add(raiser->msgbuf, message);

    longjmp(raiser->jumps[raiser->jump_pos-1], 1);
}

const char *lily_name_for_error(lily_raiser *raiser)
{
    const char *result;

    if (raiser->exception)
        result = raiser->exception->type->cls->name;
    else
        result = lily_error_names[raiser->error_code];

    return result;
}
