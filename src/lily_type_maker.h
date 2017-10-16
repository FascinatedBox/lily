#ifndef LILY_TYPE_MAKER_H
# define LILY_TYPE_MAKER_H

# include "lily_core_types.h"

typedef struct {
    lily_type **types;
    uint32_t pos;
    uint32_t size;
} lily_type_maker;

lily_type_maker *lily_new_type_maker(void);
void lily_tm_add(lily_type_maker *, lily_type *);
void lily_tm_add_unchecked(lily_type_maker *, lily_type *);
void lily_tm_insert(lily_type_maker *, int, lily_type *);
void lily_tm_reserve(lily_type_maker *, int);
lily_type *lily_tm_pop(lily_type_maker *);
lily_type *lily_tm_get(lily_type_maker *, int);
lily_type *lily_tm_make(lily_type_maker *, lily_class *, int);
lily_type *lily_tm_make_call(lily_type_maker *, int, lily_class *, int);
int lily_tm_pos(lily_type_maker *);
void lily_tm_restore(lily_type_maker *, int);

void lily_free_type_maker(lily_type_maker *);

lily_type *lily_new_raw_type(lily_class *);

#endif
