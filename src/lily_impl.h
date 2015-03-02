#ifndef LILY_IMPL_H
# define LILY_IMPL_H

/* This file defines functions that a lily implementation must define. */

/* This tells the runner that there's a block of text to write. How that gets
   done is up to the runner. */
void lily_impl_puts(void *, char *);

#endif
