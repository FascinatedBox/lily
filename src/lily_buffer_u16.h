#ifndef LILY_BUFFER_U16_H
# define LILY_BUFFER_U16_H

# include <inttypes.h>

typedef struct {
    uint16_t *data;
    uint16_t pos;
    uint16_t size;
    uint32_t pad;
} lily_buffer_u16;

lily_buffer_u16 *lily_new_buffer_u16(uint16_t);

void lily_u16_write_1(lily_buffer_u16 *, uint16_t);
void lily_u16_write_2(lily_buffer_u16 *, uint16_t, uint16_t);
void lily_u16_write_3(lily_buffer_u16 *, uint16_t, uint16_t, uint16_t);
void lily_u16_write_4(lily_buffer_u16 *, uint16_t, uint16_t, uint16_t, uint16_t);
void lily_u16_write_5(lily_buffer_u16 *, uint16_t, uint16_t, uint16_t, uint16_t,
        uint16_t);
void lily_u16_write_6(lily_buffer_u16 *, uint16_t, uint16_t, uint16_t, uint16_t,
        uint16_t, uint16_t);

void lily_u16_write_prep(lily_buffer_u16 *, uint16_t);

uint16_t lily_u16_pop(lily_buffer_u16 *);

#define lily_u16_pos(b) b->pos
#define lily_u16_get(b, pos) b->data[pos]
#define lily_u16_set_pos(b, what) b->pos = what
#define lily_u16_set_at(b, where, what) b->data[where] = what
void lily_u16_inject(lily_buffer_u16 *, int, uint16_t);

void lily_free_buffer_u16(lily_buffer_u16 *);

#endif
