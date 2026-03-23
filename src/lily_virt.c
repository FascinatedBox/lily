#include <string.h>

#include "lily_alloc.h"
#include "lily_core_types.h"
#include "lily_virt.h"

lily_virt_state *lily_new_virt_state(void)
{
    lily_virt_state *result = lily_malloc(sizeof(*result));

    result->table = lily_malloc(4 * sizeof(*result->table));
    result->virts = lily_malloc(4 * sizeof(*result->virts));

    /* This allows classes to use index 0 to grab a valid NULL table. */
    result->table[0] = NULL;
    result->table_pos = 1;
    result->table_size = 4;
    result->pos = 0;
    result->size = 4;
    result->forward_count = 0;
    return result;
}

void lily_free_virt_state(lily_virt_state *vs)
{
    for (uint16_t i = 0;i < vs->table_pos;i++)
        lily_free(vs->table[i]);

    lily_free(vs->table);
    lily_free(vs->virts);
    lily_free(vs);
}

static void grow_table(lily_virt_state *vs)
{
    vs->table_size *= 2;
    vs->table = lily_realloc(vs->table, vs->table_size * sizeof(*vs->table));
}

static void grow_virts(lily_virt_state *vs)
{
    vs->size *= 2;
    vs->virts = lily_realloc(vs->virts, vs->size * sizeof(*vs->virts));
}

static uint16_t load_forwards(lily_virt_state *vs, uint16_t virt_index)
{
    lily_function_val **virts = vs->table[virt_index];
    uint16_t count = 0, i = 0;

    for (i = 0;virts[i];i++) {
        if (virts[i]->code == NULL)
            count++;
    }

    vs->forward_count = count;
    return i;
}

void lily_vs_load_parent_virts(lily_virt_state *vs, lily_class *cls)
{
    lily_class *parent = cls->parent;

    vs->forward_count = 0;

    if (parent == NULL || parent->virt_index == 0)
        vs->pos = 0;
    else {
        uint16_t virt_index = parent->virt_index;
        uint16_t count = load_forwards(vs, virt_index);

        memcpy(vs->virts, vs->table[virt_index], count * sizeof(*vs->virts));
        vs->pos = count;
    }
}

void lily_vs_save_virts(lily_virt_state *vs, lily_class *cls)
{
    if (vs->table_pos == vs->table_size)
        grow_table(vs);

    if (vs->pos == vs->size)
        grow_virts(vs);

    /* Terminate the table so loading knows where to stop. */
    vs->virts[vs->pos] = NULL;
    vs->pos++;

    lily_function_val **virts = lily_malloc(sizeof(*virts) * vs->pos);

    /* Don't bother fixing vs->pos back to 0 here: Parser will do it when the
       next class is entered. */
    memcpy(virts, vs->virts, vs->pos * sizeof(*virts));
    cls->virt_index = vs->table_pos;
    vs->table[vs->table_pos] = virts;

    if (vs->forward_count) {
        load_forwards(vs, vs->table_pos);

        if (vs->forward_count)
            cls->flags |= CLS_HAS_MISSING_VIRTS;
    }

    vs->table_pos++;
}

void lily_vs_register_virt(lily_virt_state *vs, lily_var *var, uint16_t spot,
        lily_function_val *f)
{
    if (spot == UINT16_MAX) {
        if (vs->pos == vs->size)
            grow_virts(vs);

        var->virt_spot = vs->pos;
        vs->virts[vs->pos] = f;
        vs->pos++;
    }
    else {
        vs->virts[spot] = f;
        var->virt_spot = spot;
    }

    if (var->flags & SYM_IS_FORWARD) {
        vs->forward_count++;
        var->item_kind = ITEM_FORWARD_VIRT;
    }
    else
        var->item_kind = ITEM_VIRTUAL_METHOD;
}

struct lily_function_val_ *lily_vs_next_forward(lily_virt_state *vs,
        uint16_t virt_index, uint16_t *index)
{
    lily_function_val **virts = vs->table[virt_index];
    uint16_t i = *index;
    lily_function_val *f;

    do {
        f = virts[i];
        i++;
    } while (f && f->code != NULL);

    *index = i;
    return f;
}

struct lily_function_val_ **lily_vs_make_dyna_vtable(lily_virt_state *vs,
        lily_class *cls, uint16_t size)
{
    lily_function_val **result = lily_malloc((size + 1) * sizeof(*result));

    result[size] = NULL;

    if (cls->parent == NULL || cls->parent->virt_index == 0)
        return result;

    /* Copy NULL-terminated parent vtable into child. */
    lily_function_val **source = vs->table[cls->parent->virt_index];

    for (uint16_t i = 0;source[i];i++)
        result[i] = source[i];

    return result;
}

void lily_vs_finish_dyna_vtable(lily_virt_state *vs, lily_class *cls,
        struct lily_function_val_ **virts)
{
    if (vs->table_pos == vs->table_size)
        grow_table(vs);

    cls->virt_index = vs->table_pos;
    vs->table[vs->table_pos] = virts;
    vs->table_pos++;
}
