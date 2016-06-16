#ifndef LILY_BUFFER_U16_H
# define LILY_BUFFER_U16_H

# include <inttypes.h>

typedef struct {
    uint16_t *data;
    uint32_t pos;
    uint32_t size;
} lily_buffer_u16;

lily_buffer_u16 *lily_new_buffer_u16(uint32_t);

void lily_u16_write_1(lily_buffer_u16 *, uint16_t);
void lily_u16_write_2(lily_buffer_u16 *, uint16_t, uint16_t);
uint16_t lily_u16_pop(lily_buffer_u16 *);

void lily_u16_inject(lily_buffer_u16 *, int, uint16_t);
void lily_free_buffer_u16(lily_buffer_u16 *);

#endif
