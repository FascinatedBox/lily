#include "lily_value_stack.h"

#include "lily_api_alloc.h"

lily_value_stack *lily_new_value_stack(void)
{
    lily_value_stack *vs = lily_malloc(sizeof(lily_value_stack));

    vs->data = lily_malloc(4 * sizeof(lily_value *));
    vs->pos = 0;
    vs->size = 4;

    return vs;
}

void lily_vs_push(lily_value_stack *vs, lily_value *value)
{
    if (vs->pos + 1 > vs->size) {
        vs->size *= 2;
        vs->data = lily_realloc(vs->data, vs->size * sizeof(lily_value *));
    }

    vs->data[vs->pos] = value;
    vs->pos++;
}

lily_value *lily_vs_pop(lily_value_stack *vs)
{
    vs->pos--;
    lily_value *result = vs->data[vs->pos];
    return result;
}

void lily_free_value_stack(lily_value_stack *vs)
{
    int i;
    for (i = 0;i < vs->pos;i++)
        lily_free(vs->data[i]);

    lily_free(vs->data);
    lily_free(vs);
}
