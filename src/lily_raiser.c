#include <stdarg.h>
#include <string.h>

#include "lily_raiser.h"
#include "lily_alloc.h"

#define HANDLE_VARARGS \
lily_mb_flush(raiser->msgbuf); \
 \
va_list var_args; \
va_start(var_args, fmt); \
lily_mb_add_fmt_va(raiser->msgbuf, fmt, var_args); \
va_end(var_args);

lily_raiser *lily_new_raiser(void)
{
    lily_raiser *raiser = lily_malloc(sizeof(*raiser));
    lily_jump_link *first_jump = lily_malloc(sizeof(*first_jump));
    first_jump->prev = NULL;
    first_jump->next = NULL;

    raiser->msgbuf = lily_new_msgbuf(64);
    raiser->aux_msgbuf = lily_new_msgbuf(64);
    raiser->all_jumps = first_jump;
    raiser->source = err_from_none;

    return raiser;
}

void lily_free_raiser(lily_raiser *raiser)
{
    lily_jump_link *jump_next;

    /* The vm pulls back all_jumps during exception capture and native function
       exit. By the time this function is called, all_jumps is always the first
       in the chain. */
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
        lily_jump_link *new_link = lily_malloc(sizeof(*new_link));
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

void lily_raise_class(lily_raiser *raiser, struct lily_class_ *error_class,
                      const char *message)
{
    raiser->source = err_from_vm;
    raiser->error_class = error_class;

    /* This function doesn't need multiple arguments because it's copying over
       the message that the vm already processed. */

    lily_mb_flush(raiser->msgbuf);
    lily_mb_add(raiser->msgbuf, message);

    longjmp(raiser->all_jumps->jump, 1);
}

void lily_raise_raw(lily_raiser *raiser, const char *fmt, ...)
{
    raiser->source = err_from_raw;

    HANDLE_VARARGS

    longjmp(raiser->all_jumps->jump, 1);
}

void lily_raise_syn(lily_raiser *raiser, const char *fmt, ...)
{
    raiser->source = err_from_parse;

    HANDLE_VARARGS

    longjmp(raiser->all_jumps->jump, 1);
}

void lily_raise_tree(lily_raiser *raiser, struct lily_ast_ *error_ast,
                     const char *fmt, ...)
{
    raiser->source = err_from_emit;
    raiser->error_ast = error_ast;

    HANDLE_VARARGS

    longjmp(raiser->all_jumps->jump, 1);
}
