#ifndef LILY_TYPE_MAKER_H
# define LILY_TYPE_MAKER_H

# include "lily_core_types.h"

typedef struct {
    lily_type **types;
    uint32_t pos;
    uint32_t size;

    /* This is the default type of class 'any'. This type is used when a
       concrete type is needed and there is nothing to go on. */
    lily_type *any_class_type;
} lily_type_maker;

lily_type_maker *lily_new_type_maker(void);
void lily_tm_add(lily_type_maker *, lily_type *);
void lily_tm_add_unchecked(lily_type_maker *, lily_type *);
void lily_tm_insert(lily_type_maker *, int, lily_type *);
void lily_tm_reserve(lily_type_maker *, int);
lily_type *lily_tm_pop(lily_type_maker *);
lily_type *lily_tm_get(lily_type_maker *, int);
lily_type *lily_tm_make(lily_type_maker *, int, lily_class *, int);
lily_type *lily_tm_make_variant_result(lily_type_maker *, lily_class *, int,
        int);
lily_type *lily_tm_make_default_for(lily_type_maker *, lily_class *);
lily_type *lily_tm_make_enum_by_variant(lily_type_maker *, lily_type *);
void lily_tm_set_circular(lily_class *);

void lily_free_type_maker(lily_type_maker *);

#endif
