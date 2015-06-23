#ifndef LILY_MSGBUF_H
# define LILY_MSGBUF_H

# include <stdarg.h>
# include <stdint.h>

# include "lily_core_types.h"

/* Don't include lily_core_types.h when this is all that's needed from it. */
struct lily_type_t;

/* This is shared by different modules of the interpreter for different reasons.
   Raiser uses it for formatting error messages, and debug uses it for holding
   literals for printing. */
typedef struct {
    /* The message being stored. */
    char *message;
    /* The size that the message currently takes. */
    uint32_t pos;
    /* The buffer space allocated for the message. */
    uint32_t size;
} lily_msgbuf;

void lily_free_msgbuf(lily_msgbuf *);
lily_msgbuf *lily_new_msgbuf(lily_options *);
void lily_msgbuf_add(lily_msgbuf *, const char *);
void lily_msgbuf_add_char(lily_msgbuf *, char);
void lily_msgbuf_add_text_range(lily_msgbuf *, const char *, int, int);
void lily_msgbuf_add_int(lily_msgbuf *, int);
void lily_msgbuf_add_double(lily_msgbuf *, double);
void lily_msgbuf_add_type(lily_msgbuf *, struct lily_type_ *);
void lily_msgbuf_add_simple_value(lily_msgbuf *, lily_value *);
void lily_msgbuf_add_fmt(lily_msgbuf *, const char *, ...);
void lily_msgbuf_add_fmt_va(lily_msgbuf *, const char *, va_list);
void lily_msgbuf_escaped_add_str(lily_msgbuf *, const char *);
void lily_msgbuf_grow(lily_msgbuf *);
void lily_msgbuf_flush(lily_msgbuf *);

#endif
