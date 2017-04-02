#include "lily_alloc.h"

void *lily_malloc(size_t size)
{
    void *result = malloc(size);
    if (result == NULL)
        abort();

    return result;
}

void *lily_realloc(void *ptr, size_t new_size)
{
    void *result = realloc(ptr, new_size);
    if (result == NULL)
        abort();

    return result;
}

void lily_free(void *ptr)
{
    free(ptr);
}
