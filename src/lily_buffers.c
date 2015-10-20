#include <string.h>

#include "lily_alloc.h"
#include "lily_buffers.h"

#define CREATE_BUFFER_FUNCTIONS_FOR(name, type) \
lily_##name##_buffer *lily_new_##name(int start) \
{ \
    lily_##name##_buffer *b = lily_malloc(sizeof(lily_##name##_buffer)); \
    b->data = lily_malloc(start * sizeof(type)); \
    b->pos = 0; \
    b->size = start; \
    return b; \
} \
 \
void lily_##name##_push(lily_##name##_buffer *b, type value) \
{ \
    if (b->pos + 1 == b->size) { \
        b->size *= 2; \
        b->data = lily_realloc(b->data, b->size * sizeof(type)); \
    } \
 \
    b->data[b->pos] = value; \
    b->pos++; \
} \
 \
type lily_##name##_pop(lily_##name##_buffer *b) \
{ \
    type result = b->data[b->pos - 1]; \
    b->pos--; \
    return result; \
}

CREATE_BUFFER_FUNCTIONS_FOR(u16, uint16_t);

/* Only int16_t needs this, so it's only for that type. */

void lily_u16_inject(lily_u16_buffer *b, int where, uint16_t value)
{
    if (b->pos + 1 == b->size) {
        b->size *= 2;
        b->data = lily_realloc(b->data, b->size * sizeof(uint16_t));
    }

    int move_by;

    move_by = b->pos - where;

    memmove(b->data+where+1, b->data+where, move_by * sizeof(uint16_t));
    b->pos++;
    b->data[where] = value;
}

void lily_free_buffer(void *b)
{
    lily_free(((lily_u16_buffer *)b)->data); \
    lily_free(b);
}
