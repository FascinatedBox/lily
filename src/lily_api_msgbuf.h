#ifndef LILY_API_MSGBUF_H
# define LILY_API_MSGBUF_H

# include <stdarg.h>
# include <stdint.h>

typedef struct lily_msgbuf_ lily_msgbuf;

void lily_free_msgbuf(lily_msgbuf *);
lily_msgbuf *lily_new_msgbuf(void);
void lily_mb_add(lily_msgbuf *, const char *);
void lily_mb_add_char(lily_msgbuf *, char);
void lily_mb_add_range(lily_msgbuf *, const char *, int, int);
void lily_mb_add_boolean(lily_msgbuf *, int);
void lily_mb_add_int(lily_msgbuf *, int);
void lily_mb_add_double(lily_msgbuf *, double);
void lily_mb_add_bytestring(lily_msgbuf *,const char *, int);
void lily_mb_add_fmt(lily_msgbuf *, const char *, ...);
void lily_mb_add_fmt_va(lily_msgbuf *, const char *, va_list);
void lily_mb_flush(lily_msgbuf *);
const char *lily_mb_get(lily_msgbuf *);

#endif
