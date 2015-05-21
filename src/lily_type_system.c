#include <stdio.h>

#include "lily_alloc.h"
#include "lily_type_system.h"


# define ENSURE_TYPE_STACK(new_size) \
if (new_size >= ts->max) \
    grow_types(ts);


lily_type_system *lily_new_type_system(lily_options *options,
        lily_symtab *symtab, lily_raiser *raiser)
{
    lily_type_system *ts = lily_malloc(sizeof(lily_type_system));
    lily_type **types = lily_malloc(4 * sizeof(lily_type *));

    ts->raiser = raiser;
    ts->symtab = symtab;
    ts->types = types;
    ts->pos = 0;
    ts->max = 4;
    ts->max_seen = 0;
    ts->ceiling = 0;

    return ts;
}

void lily_free_type_system(lily_type_system *ts)
{
    if (ts)
        lily_free(ts->types);

    lily_free(ts);
}

static void grow_types(lily_type_system *ts)
{
    ts->max *= 2;
    ts->types = lily_realloc(ts->types,
            sizeof(lily_type *) * ts->max);;
}

static lily_type *deep_type_build(lily_type_system *ts, int generic_index,
        lily_type *type)
{
    lily_type *ret = type;

    if (type == NULL)
        /* functions use NULL to indicate they don't return a value. */
        ret = NULL;
    else if (type->subtypes != NULL) {
        int i, save_start;
        lily_type **subtypes = type->subtypes;
        ENSURE_TYPE_STACK(ts->pos + type->subtype_count)

        save_start = ts->pos;

        for (i = 0;i < type->subtype_count;i++) {
            lily_type *inner_type = deep_type_build(ts, generic_index,
                    subtypes[i]);
            ts->types[ts->pos] = inner_type;
            ts->pos++;
        }

        ret = lily_build_type(ts->symtab, type->cls, type->flags,
                ts->types, save_start, i);

        ts->pos -= i;
    }
    else if (type->cls->id == SYM_CLASS_GENERIC) {
        ret = ts->types[generic_index + type->generic_pos];
        /* Sometimes, a generic is wanted that was never filled in. In such a
           case, use 'any' because it is the most accepting of values. */
        if (ret == NULL) {
            ret = ts->symtab->any_class->type;
            /* This allows lambdas to determine that a given generic was not
               resolved (and prevent it). */
            ts->types[generic_index + type->generic_pos] = ret;
        }
    }
    return ret;
}

int lily_ts_check(lily_type_system *ts, lily_type *left, lily_type *right)
{
    int ret = 0;

    if (left == NULL || right == NULL)
        ret = (left == right);
    else if (left->cls->id == right->cls->id &&
             left->cls->id != SYM_CLASS_GENERIC) {
        if (right->flags & TYPE_HAS_OPTARGS) {
            /* The right side must be a function with optional arguments, with
               the left being a function (with/without optargs).

               Allow this assignment if, typewise, the --RIGHT-- has the same
               types as the left, then has optional arguments past that. (ex:
               left takes 1+ arguments, right takes 2+). We're just narrowing
               the type of the right in this case.

               This DOES NOT work in reverse (left takes 1+ arguments, but right
               takes just 1). */

            ret = 1;

            /* Remember that [0] is the return type, and always exists. */
            if (lily_ts_check(ts, left->subtypes[0], right->subtypes[0]) == 0)
                ret = 0;

            if (left->subtype_count > right->subtype_count)
                ret = 0;

            if (ret) {
                int i;
                for (i = 1;i <  left ->subtype_count;i++) {
                    lily_type *left_type = left->subtypes[i];
                    lily_type *right_type = right->subtypes[i];

                    if (right_type->cls->id == SYM_CLASS_OPTARG &&
                        left_type->cls->id != SYM_CLASS_OPTARG) {
                        right_type = right_type->subtypes[0];
                    }

                    if (lily_ts_check(ts, left_type, right_type) == 0) {
                        ret = 0;
                        break;
                    }
                }
            }
        }
        else if (left->subtype_count == right->subtype_count) {
            ret = 1;

            lily_type **left_subtypes = left->subtypes;
            lily_type **right_subtypes = right->subtypes;
            int i;
            /* Simple types have subtype_count as 0, so they'll skip this and
               yield 1. */
            for (i = 0;i < left->subtype_count;i++) {
                lily_type *left_entry = left_subtypes[i];
                lily_type *right_entry = right_subtypes[i];
                if (left_entry != right_entry &&
                    lily_ts_check(ts, left_entry, right_entry) == 0) {
                    ret = 0;
                    break;
                }
            }
        }
    }
    else if (left->cls->id == SYM_CLASS_GENERIC) {
        int generic_pos = ts->pos + left->generic_pos;
        ret = 1;
        if (ts->types[generic_pos] == NULL)
            ts->types[generic_pos] = right;
        else if (ts->types[generic_pos] != right)
            ret = 0;
    }
    else {
        if (left->cls->flags & CLS_ENUM_CLASS &&
            right->cls->flags & CLS_VARIANT_CLASS &&
            right->cls->parent == left->cls) {
            /* The right is an enum class that is a member of the left.
               Consider it valid if the right's types (if any) match to all the
               types collected -so far- for the left. */

            ret = 1;

            if (right->cls->variant_type->subtype_count != 0) {
                /* I think this is best explained as an example:
                   'enum class Option[A, B] { Some(A), None }'
                   In this case, the variant type of Some is defined as:
                   'function (A => Some[A])'
                   This pulls the 'Some[A]'. */
                lily_type *variant_output = right->cls->variant_type->subtypes[0];
                int i;
                /* The result is an Option[A, B], but Some only has A. Match up
                   generics that are available, to proper positions in the
                   parent. If any fail, then stop. */
                for (i = 0;i < variant_output->subtype_count;i++) {
                    int pos = variant_output->subtypes[i]->generic_pos;
                    ret = lily_ts_check(ts, left->subtypes[pos],
                            right->subtypes[i]);
                    if (ret == 0)
                        break;
                }
            }
            /* else it takes no arguments and is automatically correct because
               is nothing that could go wrong. */
        }
    }

    return ret;
}

inline lily_type *lily_ts_easy_resolve(lily_type_system *ts, lily_type *t)
{
    return ts->types[ts->pos + t->generic_pos];
}

lily_type *lily_ts_resolve(lily_type_system *ts, lily_type *type)
{
    int save_generic_index = ts->pos;

    ts->pos += ts->ceiling;
    lily_type *ret = deep_type_build(ts, save_generic_index, type);
    ts->pos -= ts->ceiling;

    return ret;
}

lily_type *lily_ts_resolve_by_second(lily_type_system *ts, lily_type *first,
        lily_type *second)
{
    int stack_start = ts->pos + ts->ceiling + 1;
    int save_ssp = ts->pos;

    ENSURE_TYPE_STACK(stack_start + first->subtype_count)

    int i;
    for (i = 0;i < first->subtype_count;i++)
        ts->types[stack_start + i] = first->subtypes[i];

    ts->pos = stack_start;
    lily_type *result_type = lily_ts_resolve(ts, second);
    ts->pos = save_ssp;

    return result_type;
}

void lily_ts_resolve_as_variant_by_enum(lily_type_system *ts,
        lily_type *call_result, lily_type *enum_type)
{
    lily_type *variant_type = call_result->cls->variant_type->subtypes[0];
    int max = call_result->subtype_count;
    int i;

    for (i = 0;i < max;i++) {
        int pos = variant_type->subtypes[0]->generic_pos;
        ts->types[ts->pos + pos] = enum_type->subtypes[pos];
    }
}

void lily_ts_resolve_as_self(lily_type_system *ts)
{
    int i, stop;
    lily_type *type_iter = ts->symtab->generic_type_start;

    stop = ts->pos + ts->ceiling;
    for (i = ts->pos;i < stop;i++, type_iter = type_iter->next) {
        if (ts->types[i] == NULL)
            ts->types[i] = type_iter;
    }
}

int lily_ts_raise_ceiling(lily_type_system *ts)
{
    int old_ceiling = ts->ceiling;
    int i;

    ENSURE_TYPE_STACK(ts->pos + ts->ceiling + ts->max_seen);
    ts->pos += ts->ceiling;
    ts->ceiling = ts->max_seen;
    for (i = 0;i < ts->max_seen;i++)
        ts->types[ts->pos + i] = NULL;

    return old_ceiling;
}

inline void lily_ts_lower_ceiling(lily_type_system *ts, int old_ceiling)
{
    ts->pos -= old_ceiling;
    ts->ceiling = old_ceiling;
}

void lily_ts_zap_ceiling_types(lily_type_system *ts, int num_types)
{
    num_types = ts->max_seen;
    ENSURE_TYPE_STACK(ts->pos + ts->ceiling + 1 + num_types)

    int i, max;
    for (i = ts->pos + ts->ceiling + 1, max = i + num_types;
         i < max;
         i++) {
        ts->types[i] = NULL;
    }
}

inline void lily_ts_set_ceiling_type(lily_type_system *ts, lily_type *type,
        int pos)
{
    ts->types[ts->pos + ts->ceiling + 1 + pos] = type;
}

inline lily_type *lily_ts_get_ceiling_type(lily_type_system *ts, int pos)
{
    return ts->types[ts->pos + ts->ceiling + 1 + pos];
}

inline lily_type *lily_ts_build_by_ceiling(lily_type_system *ts,
        lily_class *cls, int num_types, int flags)
{
    return lily_build_type(ts->symtab, cls, flags, ts->types,
            ts->pos + ts->ceiling + 1, num_types);
}

lily_type *lily_ts_build_enum_by_variant(lily_type_system *ts,
        lily_type *variant_type)
{
   /* The parent of a variant class is always the enum class it belongs to.
       The 'variant_type' of an enum class is the type captured when
       parsing it, and so it contains all the generics needed. */
    lily_type *parent_variant_type = variant_type->cls->parent->variant_type;

    int types_needed = ts->pos + ts->ceiling + 1 +
            parent_variant_type->subtype_count;
    ENSURE_TYPE_STACK(types_needed)

    /* If the variant takes no values, then the variant type is simply the
       default type for the class.
       If it does, then it's a function with the return (at [0]) being the
       variant result. */
    lily_type *child_result;

    if (variant_type->cls->variant_type->subtype_count != 0)
        child_result = variant_type->cls->variant_type->subtypes[0];
    else
        child_result = NULL;

    lily_type *any_type = ts->symtab->any_class->type;

    /* Sometimes, a variant class does not use all of the generics provided by
       the parent enum class. In that case, the class 'any' will be used. */
    int i, j;
    for (i = 0, j = 0;i < parent_variant_type->subtype_count;i++) {
        if (child_result &&
            child_result->subtype_count > j &&
            child_result->subtypes[j]->generic_pos == i) {
            lily_ts_set_ceiling_type(ts, variant_type->subtypes[j], i);
            j++;
        }
        else
            lily_ts_set_ceiling_type(ts, any_type, i);
    }

    return lily_ts_build_by_ceiling(ts, variant_type->cls->parent, i, 0);
}

int lily_ts_count_unresolved(lily_type_system *ts)
{
    int count = 0, top = ts->pos + ts->ceiling;
    int i;
    for (i = ts->pos;i < top;i++) {
        if (ts->types[i] == NULL)
            count++;
    }

    return count;
}

void lily_ts_generics_seen(lily_type_system *ts, int amount)
{
    if (amount > ts->max_seen)
        ts->max_seen = amount;
}