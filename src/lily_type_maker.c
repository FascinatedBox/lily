#include <string.h>

#include "lily_alloc.h"
#include "lily_type_maker.h"

/* These are the TYPE_* flags that bubble up through types (it's written onto a
   type if any subtypes have them). */
#define BUBBLE_FLAGS \
    (TYPE_MAYBE_CIRCULAR | TYPE_IS_UNRESOLVED | TYPE_IS_INCOMPLETE)

lily_type_maker *lily_new_type_maker(void)
{
    lily_type_maker *tm = lily_malloc(sizeof(lily_type_maker));

    tm->types = lily_malloc(sizeof(lily_type *) * 4);
    tm->pos = 0;
    tm->size = 4;

    return tm;
}

static lily_type *make_new_type(lily_class *cls)
{
    lily_type *new_type = lily_malloc(sizeof(lily_type));
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

        tm->types = lily_realloc(tm->types, sizeof(lily_type *) * tm->size);
    }
}

inline void lily_tm_add_unchecked(lily_type_maker *tm, lily_type *type)
{
    tm->types[tm->pos] = type;
    tm->pos++;
}

void lily_tm_add(lily_type_maker *tm, lily_type *type)
{
    if (tm->pos + 1 == tm->size) {
        tm->size *= 2;
        tm->types = lily_realloc(tm->types, sizeof(lily_type *) * tm->size);
    }

    tm->types[tm->pos] = type;
    tm->pos++;
}

void lily_tm_insert(lily_type_maker *tm, int pos, lily_type *type)
{
    if (pos >= tm->size) {
        while (pos >= tm->size)
            tm->size *= 2;

        tm->types = lily_realloc(tm->types, sizeof(lily_type *) * tm->size);
    }

    tm->types[pos] = type;
}

inline lily_type *lily_tm_get(lily_type_maker *tm, int pos)
{
    return tm->types[pos];
}

void lily_tm_set_circular(lily_class *cls)
{
    /* The only types that could have been made are types that are of this class. If
       there are any, force them to be marked as being circular. */
    lily_type *type_iter = cls->all_subtypes;
    while (type_iter) {
        type_iter->flags |= TYPE_MAYBE_CIRCULAR;
        type_iter = type_iter->next;
    }

    /* Make sure any type which includes the current class in any variation will get
        a marker since the class needs a marker. */
    cls->flags |= CLS_ALWAYS_MARK;
}

/*  lookup_type
    Determine if the current type exists in the symtab.

    Success: The type from the symtab is returned.
    Failure: NULL is returned. */
static lily_type *lookup_type(lily_type *input_type)
{
    lily_type *iter_type = input_type->cls->all_subtypes;
    lily_type *ret = NULL;

    while (iter_type) {
        if (iter_type->subtypes      != NULL &&
            iter_type->subtype_count == input_type->subtype_count &&
            (iter_type->flags & ~BUBBLE_FLAGS) ==
                (input_type->flags & ~BUBBLE_FLAGS)) {
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

/*  finalize_type
    Determine if the given type is circular or unresolved. It's considered
    either if it contains things that are either of those. */
static void finalize_type(lily_type *input_type)
{
    int cls_flags = 0;
    if (input_type->subtypes) {
        int i;
        for (i = 0;i < input_type->subtype_count;i++) {
            lily_type *subtype = input_type->subtypes[i];
            if (subtype) {
                input_type->flags |= subtype->flags & BUBBLE_FLAGS;
                cls_flags |= subtype->cls->flags;
            }
        }
    }

    /* If the current class has been put into a container (functions are not containers),
       then consider the class circular. This is not perfect: It will make classes
       circular if, say, they have a method that takes a list of them.
       This method stinks, but at least it doesn't require rewalking types to update
       circularity, which stinks more. */
    if (cls_flags & CLS_IS_CURRENT &&
        input_type->cls->id != SYM_CLASS_FUNCTION) {

        int i;
        lily_class *current_cls = NULL;
        for (i = 0;i < input_type->subtype_count;i++) {
            lily_type *subtype = input_type->subtypes[i];
            if (subtype && subtype->cls->flags & CLS_IS_CURRENT)
                current_cls = subtype->cls;
        }

        lily_tm_set_circular(current_cls);
    }

    /* If something contains a type that needs to be marked, then the container
       will need a marker as well as a precaution. */
    if ((cls_flags | input_type->cls->flags) & CLS_ALWAYS_MARK)
        input_type->flags |= TYPE_MAYBE_CIRCULAR;
}

static lily_type *build_real_type_for(lily_type *fake_type)
{
    lily_type *new_type = make_new_type(fake_type->cls);
    int count = fake_type->subtype_count;

    memcpy(new_type, fake_type, sizeof(lily_type));

    if (count) {
        lily_type **new_subtypes = lily_malloc(count *
                sizeof(lily_type *));
        memcpy(new_subtypes, fake_type->subtypes, count *
                sizeof(lily_type *));
        new_type->subtypes = new_subtypes;
    }
    else {
        new_type->subtypes = NULL;
        /* If this type has no subtypes, and it was just created, then it
            has to be the default type for this class. */
        fake_type->cls->type = new_type;
    }

    new_type->subtype_count = count;
    new_type->next = new_type->cls->all_subtypes;
    new_type->cls->all_subtypes = new_type;

    finalize_type(new_type);
    return new_type;
}

lily_type *lily_tm_make(lily_type_maker *tm, int flags, lily_class *cls,
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
    if (result_type == NULL)
        result_type = build_real_type_for(&fake_type);

    tm->pos -= num_entries;
    return result_type;
}

static void walk_for_generics(lily_type_maker *tm, lily_type **use_map,
        lily_type *type, int offset, int *max)
{
    if (type) {
        if (type->subtypes) {
            int i;
            for (i = 0;i < type->subtype_count;i++)
                walk_for_generics(tm, use_map, type->subtypes[i], offset, max);
        }
        else if (type->cls->id == SYM_CLASS_GENERIC) {
            int pos = type->generic_pos;
            if (pos > *max)
                *max = pos;

            use_map[pos] = type;
        }
    }
}

lily_type *lily_tm_make_variant_result(lily_type_maker *tm, lily_class *variant_cls,
        int start, int end)
{
    int i, max_seen = -1;
    lily_type *use_map[32];

    for (i = 0;i < 32;i++)
        use_map[i] = NULL;

    for (i = start;i < end;i++)
        walk_for_generics(tm, use_map, tm->types[i], tm->pos, &max_seen);

    /* Necessary because generic positions start at 0 instead of 1. */
    max_seen++;
    int j = 0;

    for (i = 0;i < max_seen;i++) {
        if (use_map[i]) {
            lily_tm_add(tm, use_map[i]);
            j++;
        }
    }

    lily_type *result = lily_tm_make(tm, 0, variant_cls, max_seen);

    return result;
}

lily_type *lily_tm_make_enum_by_variant(lily_type_maker *tm,
        lily_type *variant)
{
    /* The parent of a variant is always the enum that it is part of. */
    lily_class *enum_cls = variant->cls->parent;
    lily_type *result;

    /* If the enum does not take subtypes, then it will have a default type. */
    if (enum_cls->generic_count == 0)
        result = enum_cls->type;
    else {
        int start = tm->pos;
        int generic_need = enum_cls->generic_count;
        int i;
        for (i = 0;i < generic_need;i++)
            lily_tm_add(tm, tm->any_class_type);

        if (variant->cls->variant_type->subtype_count) {
            /* This type describes the result of invoking the variant as a
               function. Use this to map the incoming variant to an enum using
               the generic positions. As usual, the [0] is to get the result
               type of that function-like entity. */
            lily_type *mapping_type = variant->cls->variant_type->subtypes[0];
            int i;
            for (i = 0;i < mapping_type->subtype_count;i++) {
                lily_type *t = mapping_type->subtypes[i];
                lily_tm_insert(tm, start + t->generic_pos, variant->subtypes[i]);
            }
        }
        /* else the variant does not take arguments. As such, it gets an enum
           with all generics defaulted to type any. */

        result = lily_tm_make(tm, 0, enum_cls, generic_need);
    }

    return result;
}

lily_type *lily_tm_make_default_for(lily_type_maker *tm, lily_class *cls)
{
    lily_type *new_type = make_new_type(cls);
    cls->type = new_type;
    cls->all_subtypes = new_type;

    return new_type;
}

void lily_free_type_maker(lily_type_maker *tm)
{
    lily_free(tm->types);
    lily_free(tm);
}
