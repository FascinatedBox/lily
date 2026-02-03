#ifndef LILY_TYPE_MAKER_H
# define LILY_TYPE_MAKER_H

# include "lily_core_types.h"

typedef struct {
    lily_type **types;
    uint16_t pos;
    uint16_t size;
    uint32_t pad;
} lily_type_maker;

lily_type_maker *lily_new_type_maker(void);
void lily_tm_add(lily_type_maker *, lily_type *);
void lily_tm_add_unchecked(lily_type_maker *, lily_type *);
void lily_tm_insert(lily_type_maker *, uint16_t, lily_type *);
void lily_tm_reserve(lily_type_maker *, uint16_t);
lily_type *lily_tm_build_empty_variant_type(lily_type_maker *, lily_class *);
lily_type *lily_tm_pop(lily_type_maker *);
lily_type *lily_tm_make(lily_type_maker *, lily_class *, uint16_t);
lily_type *lily_tm_make_call(lily_type_maker *, uint16_t, lily_class *,
        uint16_t);
uint16_t lily_tm_pos(lily_type_maker *);
void lily_tm_restore(lily_type_maker *, uint16_t);

void lily_free_type_maker(lily_type_maker *);

lily_type *lily_new_raw_type(lily_class *);

#endif
