#ifndef LILY_IMPL_H
# define LILY_IMPL_H

# include <stdio.h>
# include <stdlib.h>

/* This file defines functions that a lily implementation must define. */

/* Tells the server running lily to send a chunk of HTML data. The data is not
   to be free'd. */
void lily_impl_send_html(char *);

# define lily_malloc(size) malloc(size)
# define lily_realloc(ptr, size) realloc(ptr, size)
# define lily_free(ptr) free(ptr)

/* Used for sending debug messages. */
void lily_impl_debugf(char *, ...);
#endif
