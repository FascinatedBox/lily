#ifndef LILY_MSGBUF_H
# define LILY_MSGBUF_H

# include "lily_syminfo.h"

/* This is used if an error message involves signatures. Doing so allows the
   full signature to be printed. */
typedef struct {
    char *msg;
    int pos;
    int size;
    int has_err;
} lily_msgbuf;

void lily_free_msgbuf(lily_msgbuf *);
lily_msgbuf *lily_new_msgbuf(char *);
void lily_msgbuf_add(lily_msgbuf *, char *);
void lily_msgbuf_add_int(lily_msgbuf *, int);
void lily_msgbuf_add_sig(lily_msgbuf *, lily_sig *);

#endif
