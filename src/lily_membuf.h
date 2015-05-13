#ifndef LILY_MEMBUF_H
# define LILY_MEMBUF_H

# include <stdint.h>

/* lily_membuf holds a series of \0 terminated strings in a single char *
   buffer. The caller receives indexes to the strings inside, so that the buffer
   can grow without worrying about realloc modifying the underlying buffer. */

typedef struct  {
    char *buffer;
    uint32_t pos;
    uint32_t size;
} lily_membuf;

lily_membuf *lily_membuf_new(void);

void lily_membuf_free(lily_membuf *);

/* Add a string to the buffer, obtaining an index for later use. */
int lily_membuf_add(lily_membuf *, char *);

/* Fetch a string from the buffer. The buffer's position is rewound so that new
   strings will overwrite this string.
   Use this when the string is needed the last time. */
char *lily_membuf_fetch_restore(lily_membuf *, int);

/* Get a string from the buffer at a given position. The buffer's position is
   not moved, so the string will not be overwritten.

   Note: The returned char * is a shallow copy. It's possible for it to become
   invalid if more data is added to the membuf. */
char *lily_membuf_get(lily_membuf *, int);

/* Restores the buffer's position to a given index. This is primarily so that
   the ast pool can easily restore the buffer to where it started at the
   beginning of an expression. */
void lily_membuf_restore_to(lily_membuf *, int);

#endif
