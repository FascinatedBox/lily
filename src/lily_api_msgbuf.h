#ifndef LILY_API_MSGBUF_H
# define LILY_API_MSGBUF_H

# include <stdarg.h>
# include <stdint.h>

# ifndef LILY_STATE
#  define LILY_STATE
typedef struct lily_vm_state_ lily_state;
# endif

struct lily_value_;
typedef struct lily_msgbuf_ lily_msgbuf;

void lily_free_msgbuf(lily_msgbuf *);
lily_msgbuf *lily_new_msgbuf(uint32_t);
void lily_mb_flush(lily_msgbuf *);
const char *lily_mb_get(lily_msgbuf *);

void lily_mb_add(lily_msgbuf *, const char *);
void lily_mb_add_char(lily_msgbuf *, char);
void lily_mb_add_fmt(lily_msgbuf *, const char *, ...);
void lily_mb_add_fmt_va(lily_msgbuf *, const char *, va_list);
void lily_mb_add_slice(lily_msgbuf *, const char *, int, int);
void lily_mb_add_value(lily_msgbuf *, lily_state *, struct lily_value_ *);
const char *lily_mb_sprintf(lily_msgbuf *, const char *, ...);

/* Lily's msgbuf works by having the caller flush the msgbuf before use, so that
   problems occur close to the source. Most callers want the first call, which
   will get and flush the msgbuf for them. */
lily_msgbuf *lily_get_msgbuf(lily_state *);

/* Get the interpreter's working msgbuf, but don't flush it. */
lily_msgbuf *lily_get_msgbuf_noflush(lily_state *);

#endif
