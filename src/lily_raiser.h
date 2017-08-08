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

    /* This is where the error message is stored. */
    lily_msgbuf *msgbuf;

    /* Some messages are more difficult to write. This auxillary buffer is for
       building up more complex messages. Any part of the interpreter is free to
       take this, flush it, and use it to build their error message.
       *  */
    lily_msgbuf *aux_msgbuf;

    /* This is set when the emitter raises an error and that error does not
       reference the current line. This will be set to the actual line. It can
       be ignored when 0. */
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
