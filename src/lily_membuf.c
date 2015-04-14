#include <string.h>

#include "lily_membuf.h"

#define malloc_mem(size)             membuf->mem_func(NULL, size)
#define realloc_mem(ptr, size)       membuf->mem_func(ptr, size)
#define free_mem(ptr)          (void)membuf->mem_func(ptr, 0)

lily_membuf *lily_membuf_new(lily_mem_func mem_func, lily_raiser *raiser)
{
    lily_membuf *membuf = mem_func(NULL, sizeof(lily_membuf));

    membuf->mem_func = mem_func;
    membuf->buffer = malloc_mem(64);
    membuf->pos = 0;
    membuf->size = 63;

    return membuf;
}

void lily_membuf_free(lily_membuf *membuf)
{
    if (membuf)
        free_mem(membuf->buffer);

    free_mem(membuf);
}

int lily_membuf_add(lily_membuf *membuf, char *new_str)
{
    int result = membuf->pos;
    int want_size = membuf->pos + 1 + strlen(new_str);
    if (membuf->size < want_size) {
        while (membuf->size < want_size)
            membuf->size *= 2;

        char *new_buffer = realloc_mem(membuf->buffer, membuf->size);
        membuf->buffer = new_buffer;
    }

    /* strcpy is used here instead of strcpy so that the \0 is preserved.
       That's important because it allows callers to just ask for an index and
       get a string with no fuss. */
    strcpy(membuf->buffer + membuf->pos, new_str);
    membuf->pos = want_size;
    return result;
}

char *lily_membuf_fetch_restore(lily_membuf *membuf, int pos)
{
    char *result = membuf->buffer + pos;
    membuf->pos = pos;
    return result;
}

inline char *lily_membuf_get(lily_membuf *membuf, int pos)
{
    return membuf->buffer + pos;
}

inline void lily_membuf_restore_to(lily_membuf *membuf, int pos)
{
    membuf->pos = pos;
}

int lily_membuf_add_three(lily_membuf *membuf, char *first, char *second,
        char *third)
{
    int result_pos = lily_membuf_add(membuf, first);
    /* The pos-- is so that the beginning of the string overwrites the \0 of
       the last one, resulting in the strings being pulled out as one. */
    membuf->pos--;
    lily_membuf_add(membuf, second);
    membuf->pos--;
    lily_membuf_add(membuf, third);
    return result_pos;
}
