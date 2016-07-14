#ifndef LILY_MSGBUF_H
# define LILY_MSGBUF_H

# include <stdarg.h>
# include <stdint.h>

/* msgbuf is pretty simple: It's a sized buffer plus functions for safely adding
   data into that buffer. */
typedef struct {
    /* The message being stored. */
    char *message;
    /* The size that the message currently takes. */
    uint32_t pos;
    /* The buffer space allocated for the message. */
    uint32_t size;
} lily_msgbuf;

void lily_free_msgbuf(lily_msgbuf *);
lily_msgbuf *lily_new_msgbuf(void);
void lily_msgbuf_add(lily_msgbuf *, const char *);
void lily_msgbuf_add_char(lily_msgbuf *, char);
void lily_msgbuf_add_text_range(lily_msgbuf *, const char *, int, int);
void lily_msgbuf_add_boolean(lily_msgbuf *, int);
void lily_msgbuf_add_int(lily_msgbuf *, int);
void lily_msgbuf_add_double(lily_msgbuf *, double);
void lily_msgbuf_add_bytestring(lily_msgbuf *,const char *, int);
void lily_msgbuf_add_fmt(lily_msgbuf *, const char *, ...);
void lily_msgbuf_add_fmt_va(lily_msgbuf *, const char *, va_list);
void lily_msgbuf_remove(lily_msgbuf *, int);
void lily_msgbuf_grow(lily_msgbuf *);
void lily_msgbuf_flush(lily_msgbuf *);

#endif
