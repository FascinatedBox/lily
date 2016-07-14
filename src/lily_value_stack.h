#ifndef LILY_VALUE_STACK_H
# define LILY_VALUE_STACK_H

# include <stdint.h>

typedef struct lily_value_ lily_value;

typedef struct lily_value_stack_ {
    lily_value **data;
    uint32_t pos;
    uint32_t size;
} lily_value_stack;

lily_value_stack *lily_new_value_stack(void);
void lily_vs_push(lily_value_stack *, lily_value *);
lily_value *lily_vs_pop(lily_value_stack *);

#define lily_vs_pos(vs) vs->pos
#define lily_vs_nth(vs, n) vs->data[n]

void lily_free_value_stack(lily_value_stack *);

#endif
