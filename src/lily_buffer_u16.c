#include <string.h>

#include "lily_buffer_u16.h"

#include "lily_api_alloc.h"

lily_buffer_u16 *lily_new_buffer_u16(uint32_t start)
{
    lily_buffer_u16 *b = lily_malloc(sizeof(lily_buffer_u16));
    b->data = lily_malloc(start * sizeof(uint16_t));
    b->pos = 0;
    b->size = start;
    return b;
}

void lily_u16_write_1(lily_buffer_u16 *b, uint16_t one)
{
    if (b->pos + 1 > b->size) {
        b->size *= 2;
        b->data = lily_realloc(b->data, b->size * sizeof(uint16_t));
    }

    b->data[b->pos] = one;
    b->pos++;
}

void lily_u16_write_2(lily_buffer_u16 *b, uint16_t one, uint16_t two)
{
    if (b->pos + 2 > b->size) {
        b->size *= 2;
        b->data = lily_realloc(b->data, b->size * sizeof(uint16_t));
    }

    b->data[b->pos    ] = one;
    b->data[b->pos + 1] = two;
    b->pos += 2;
}

uint16_t lily_u16_pop(lily_buffer_u16 *b)
{
    uint16_t result = b->data[b->pos - 1];
    b->pos--;
    return result;
}

void lily_u16_inject(lily_buffer_u16 *b, int where, uint16_t value)
{
    if (b->pos + 1 == b->size) {
        b->size *= 2;
        b->data = lily_realloc(b->data, b->size * sizeof(uint16_t));
    }

    int move_by = b->pos - where;

    memmove(b->data+where+1, b->data+where, move_by * sizeof(uint16_t));
    b->pos++;
    b->data[where] = value;
}

void lily_free_buffer_u16(lily_buffer_u16 *b)
{
    lily_free(b->data);
    lily_free(b);
}
