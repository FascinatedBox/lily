#ifndef LILY_bufferFERS_H
# define LILY_bufferFERS_H

# include <inttypes.h>
# include "lily_core_types.h"

# define DECLARE_BUFFER_OF(name, type) \
typedef struct { \
    type *data; \
    uint32_t pos; \
    uint32_t size; \
} lily_##name##_buffer; \
 \
lily_##name##_buffer *lily_new_##name(int); \
void lily_##name##_push  (lily_##name##_buffer *, type data); \
type lily_##name##_pop   (lily_##name##_buffer *);

DECLARE_BUFFER_OF(u16, uint16_t);
DECLARE_BUFFER_OF(type, lily_type *);

void lily_u16_inject(lily_u16_buffer *, int, uint16_t);
void lily_free_buffer(void *);

#endif
