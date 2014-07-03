#ifndef LILY_IMPL_H
# define LILY_IMPL_H

# include <stdio.h>
# include <stdlib.h>

/* This file defines functions that a lily implementation must define. */

/* Used for sending debug messages. */
void lily_impl_debugf(char *, ...);

/* Tells the server running lily to send a chunk of HTML data. The data is not
   to be free'd. */
void lily_impl_send_html(char *);

#ifndef AFT_ALLOC
# define lily_malloc(size) malloc(size)
# define lily_realloc(ptr, size) realloc(ptr, size)
# define lily_free(ptr) free(ptr)
#else
# define lily_malloc(size) aft_malloc(__FILE__, __LINE__, size)
# define lily_realloc(ptr, size) aft_realloc(__FILE__, __LINE__, ptr, size)
# define lily_free(ptr) aft_free(__FILE__, __LINE__, ptr)
extern void *aft_malloc(char *, int, size_t);
extern void *aft_realloc(char *, int, void *, size_t);
extern void aft_free(char *, int, void *);
#endif
#endif
