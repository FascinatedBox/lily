#ifndef LILY_RAISER_H
# define LILY_RAISER_H

# include <setjmp.h>

# include "lily.h"
# include "lily_core_types.h"

typedef struct lily_jump_link_ {
    struct lily_jump_link_ *prev;
    struct lily_jump_link_ *next;

    jmp_buf jump;
} lily_jump_link;

typedef struct lily_raiser_ {
    lily_jump_link *all_jumps;

    /* The error message is stored here. */
    lily_msgbuf *msgbuf;

    /* This is a spare msgbuf for building error messages. */
    lily_msgbuf *aux_msgbuf;

    /* Errors raised during parsing use the lexer's current line number as a
       reference. This doesn't work for errors raised by emitter, which may
       target a section on a previous line. In such cases, the emitter will set
       this to a non-zero value. */
    uint32_t line_adjust;
    uint32_t is_syn_error;
} lily_raiser;

lily_raiser *lily_new_raiser(void);
void lily_free_raiser(lily_raiser *);
void lily_raise_syn(lily_raiser *, const char *, ...);
void lily_raise_err(lily_raiser *, const char *, ...);
lily_jump_link *lily_jump_setup(lily_raiser *);
void lily_release_jump(lily_raiser *);

const char *lily_name_for_error(lily_raiser *);

#endif
