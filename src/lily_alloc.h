#ifndef LILY_ALLOC_H
# define LILY_ALLOC_H

# include <stdlib.h>

void *lily_malloc(size_t);
void *lily_realloc(void *, size_t);
#define lily_free(ptr) free(ptr)

#endif
