#ifndef LILY_IMPL_H
# define LILY_IMPL_H

# include <stdio.h>
# include <stdlib.h>

/* This file defines functions that a lily implementation must define. */

/* This tells the runner that there's a block of text to write. How that gets
   done is up to the runner. */
void lily_impl_puts(char *);

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
