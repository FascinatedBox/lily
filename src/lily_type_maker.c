#include <string.h>

#include "lily_alloc.h"
#include "lily_type_maker.h"

/* These are the TYPE_* flags that bubble up through types (it's written onto a
   type if any subtypes have them). */
#define BUBBLE_FLAGS \
    (TYPE_IS_UNRESOLVED | TYPE_IS_INCOMPLETE | TYPE_HAS_SCOOP | \
     TYPE_HAS_OPTARGS | TYPE_TO_BLOCK)

lily_type_maker *lily_new_type_maker(void)
{
    lily_type_maker *tm = lily_malloc(sizeof(*tm));

    tm->types = lily_malloc(sizeof(*tm->types) * 4);
    tm->pos = 0;
    tm->size = 4;

    return tm;
}

lily_type *lily_new_raw_type(lily_class *cls)
{
    lily_type *new_type = lily_malloc(sizeof(*new_type));
    new_type->item_kind = ITEM_TYPE;
    new_type->cls = cls;
    new_type->flags = 0;
    new_type->generic_pos = 0;
    new_type->subtype_count = 0;
    new_type->subtypes = NULL;
    new_type->next = NULL;

    return new_type;
}

void lily_tm_reserve(lily_type_maker *tm, int amount)
{
    if (tm->pos + amount > tm->size) {
        while (tm->pos + amount > tm->size)
            tm->size *= 2;

        tm->types = lily_realloc(tm->types, sizeof(*tm->types) * tm->size);
    }
}

void lily_tm_add_unchecked(lily_type_maker *tm, lily_type *type)
{
    tm->types[tm->pos] = type;
    tm->pos++;
}

void lily_tm_add(lily_type_maker *tm, lily_type *type)
{
    if (tm->pos + 1 == tm->size) {
        tm->size *= 2;
        tm->types = lily_realloc(tm->types, sizeof(*tm->types) * tm->size);
    }

    tm->types[tm->pos] = type;
    tm->pos++;
}

void lily_tm_insert(lily_type_maker *tm, int pos, lily_type *type)
{
    tm->types[pos] = type;
}

lily_type *lily_tm_pop(lily_type_maker *tm)
{
    tm->pos--;
    lily_type *result = tm->types[tm->pos];
    return result;
}

/* Try to see if a type that describes 'input_type' already exists. If so,
   return the existing type. If not, return NULL. */
static lily_type *lookup_type(lily_type *input_type)
{
    lily_type *iter_type = input_type->cls->all_subtypes;
    lily_type *ret = NULL;

    while (iter_type) {
        if (iter_type->subtype_count == input_type->subtype_count &&
            /* All other type-based flags are irrelevant to equality. */
            (iter_type->flags & TYPE_IS_VARARGS) ==
                (input_type->flags & TYPE_IS_VARARGS)) {
            int i, match = 1;
            for (i = 0;i < iter_type->subtype_count;i++) {
                if (iter_type->subtypes[i] != input_type->subtypes[i]) {
                    match = 0;
                    break;
                }
            }

            if (match == 1) {
                ret = iter_type;
                break;
            }
        }

        iter_type = iter_type->next;
    }

    return ret;
}

static lily_type *build_real_type_for(lily_type *fake_type)
{
    /* Given a 'fake' type (one made off the stack), create a real type and add
       it to the types of a class. Don't worry about setting self_type, because
       parser takes care of it. */
    lily_type *new_type = lily_new_raw_type(fake_type->cls);

    memcpy(new_type, fake_type, sizeof(lily_type));

    int count = fake_type->subtype_count;
    lily_type **new_subtypes = lily_malloc(count * sizeof(*new_subtypes));
    memcpy(new_subtypes, fake_type->subtypes, count * sizeof(*new_subtypes));
    new_type->subtypes = new_subtypes;
    new_type->subtype_count = count;

    new_type->next = new_type->cls->all_subtypes;
    new_type->cls->all_subtypes = new_type;
    /* Any other flags bubble up from the subtypes. */
    new_type->flags &= TYPE_IS_VARARGS;

    int i;
    for (i = 0;i < new_type->subtype_count;i++) {
        lily_type *subtype = new_type->subtypes[i];
        if (subtype)
            new_type->flags |= subtype->flags & BUBBLE_FLAGS;
    }

    return new_type;
}

lily_type *lily_tm_make(lily_type_maker *tm, lily_class *cls, int num_entries)
{
    lily_type fake_type;

    fake_type.cls = cls;
    fake_type.generic_pos = 0;
    fake_type.subtypes = tm->types + (tm->pos - num_entries);
    fake_type.subtype_count = num_entries;
    fake_type.flags = 0;
    fake_type.next = NULL;

    lily_type *result_type = lookup_type(&fake_type);
    if (result_type == NULL) {
        fake_type.item_kind = ITEM_TYPE;
        result_type = build_real_type_for(&fake_type);
    }

    tm->pos -= num_entries;
    return result_type;
}

lily_type *lily_tm_make_call(lily_type_maker *tm, int flags, lily_class *cls,
        int num_entries)
{
    lily_type fake_type;

    fake_type.cls = cls;
    fake_type.generic_pos = 0;
    fake_type.subtypes = tm->types + (tm->pos - num_entries);
    fake_type.subtype_count = num_entries;
    fake_type.flags = flags;
    fake_type.next = NULL;

    lily_type *result_type = lookup_type(&fake_type);
    if (result_type == NULL) {
        fake_type.item_kind = ITEM_TYPE;
        result_type = build_real_type_for(&fake_type);
    }

    tm->pos -= num_entries;
    return result_type;
}

int lily_tm_pos(lily_type_maker *tm)
{
    return tm->pos;
}

void lily_tm_restore(lily_type_maker *tm, int pos)
{
    tm->pos = pos;
}

void lily_free_type_maker(lily_type_maker *tm)
{
    lily_free(tm->types);
    lily_free(tm);
}
