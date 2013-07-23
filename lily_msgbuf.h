#ifndef LILY_MSGBUF_H
# define LILY_MSGBUF_H

# include "lily_syminfo.h"

/* The raiser includes a message buffer to be used for holding complex messages
   not handlable through lily_raise. This typically involves messages that
   include type information. */
typedef struct {
    /* The message being stored. */
    char *message;
    /* The size that the message currently takes. */
    int pos;
    /* The buffer space allocated for the message. */
    int size;
    /* 0 by default, 1 if a function could not expand the buffer. If 1, then
       future functions will safely do nothing. Thus it is not necessary for a
       caller to check a msgbuf before adding to it. */
    int truncated;
} lily_msgbuf;

void lily_free_msgbuf(lily_msgbuf *);
lily_msgbuf *lily_new_msgbuf(void);
void lily_msgbuf_add(lily_msgbuf *, char *);
void lily_msgbuf_add_text_range(lily_msgbuf *, char *, int, int);
void lily_msgbuf_add_int(lily_msgbuf *, int);
void lily_msgbuf_add_sig(lily_msgbuf *, lily_sig *);

#endif
