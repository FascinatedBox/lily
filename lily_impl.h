#ifndef LILY_IMPL_H
# define LILY_IMPL_H

#include <stdlib.h>

/* This file defines functions that a lily implementation must define. */

/* Tells the server running lily to send a chunk of HTML data. The data is not
   to be free'd. */
void lily_impl_send_html(char *);

/* This is called when lily has encountered a fatal error. */
void lily_impl_fatal(char *, ...);

/* This is called when lily needs to allocate memory. The implementation should
   call to have the interpreter gracefully exit. Since that isn't possible yet,
   exit() is also allowed. */
void *lily_impl_malloc(size_t);

/* See above, except for realloc instead of malloc. */
void *lily_impl_realloc(void *, size_t);

/* Used for sending debug messages. */
void lily_impl_debugf(char *, ...);
#endif
