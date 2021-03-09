#include <string.h>

#include "lily_alloc.h"
#include "lily_string_pile.h"

lily_string_pile *lily_new_string_pile(void)
{
    lily_string_pile *sp = lily_malloc(sizeof(*sp));

    sp->buffer = lily_malloc(64 * sizeof(*sp->buffer));
    sp->size = 64;
    return sp;
}

void lily_free_string_pile(lily_string_pile *sp)
{
    lily_free(sp->buffer);
    lily_free(sp);
}

void lily_sp_insert(lily_string_pile *sp, const char *new_str, uint16_t *pos)
{
    uint16_t want_size = *pos + 1 + (uint16_t)strlen(new_str);

    if (sp->size < want_size) {
        while (sp->size < want_size)
            sp->size *= 2;

        char *new_buffer = lily_realloc(sp->buffer,
                sp->size * sizeof(*new_buffer));
        sp->buffer = new_buffer;
    }

    strcpy(sp->buffer + *pos, new_str);
    *pos = want_size;
}

void lily_sp_insert_bytes(lily_string_pile *sp, const char *new_str,
        uint16_t *pos, uint16_t new_str_size)
{
    uint16_t want_size = *pos + 1 + new_str_size;

    if (sp->size < want_size) {
        while (sp->size < want_size)
            sp->size *= 2;

        char *new_buffer = lily_realloc(sp->buffer,
                sp->size * sizeof(*new_buffer));
        sp->buffer = new_buffer;
    }

    memcpy(sp->buffer + *pos, new_str, new_str_size);
    *pos = want_size;
}

char *lily_sp_get(lily_string_pile *sp, uint16_t pos)
{
    return sp->buffer + pos;
}
