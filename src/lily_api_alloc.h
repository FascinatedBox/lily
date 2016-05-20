#ifndef LILY_API_ALLOC_H
# define LILY_API_ALLOC_H

# include <stdlib.h>

void *lily_malloc(size_t);
void *lily_realloc(void *, size_t);
void lily_free(void *);

#endif
