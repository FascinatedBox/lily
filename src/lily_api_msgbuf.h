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
int lily_mb_pos(lily_msgbuf *);

void lily_mb_add(lily_msgbuf *, const char *);
void lily_mb_add_char(lily_msgbuf *, char);
void lily_mb_add_fmt(lily_msgbuf *, const char *, ...);
void lily_mb_add_fmt_va(lily_msgbuf *, const char *, va_list);
void lily_mb_add_slice(lily_msgbuf *, const char *, int, int);
void lily_mb_add_value(lily_msgbuf *, lily_state *, struct lily_value_ *);
const char *lily_mb_sprintf(lily_msgbuf *, const char *, ...);
const char *lily_mb_html_escape(lily_msgbuf *, const char *);

/* Clear the data in the vm's msgbuf, and return the msgbuf. */
lily_msgbuf *lily_get_clean_msgbuf(lily_state *);

/* Just return the vm's msgbuf. The signature is different so the caller doesn't
   use this by accident. */
void lily_get_dirty_msgbuf(lily_state *, lily_msgbuf **);

#endif
