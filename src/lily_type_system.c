#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lily.h"

#include "lily_type_system.h"
#include "lily_alloc.h"

extern lily_type *lily_question_type;
extern lily_type *lily_unit_type;

# define ENSURE_TYPE_STACK(new_size) \
if (new_size >= ts->max) \
    grow_types(ts);

/* If this is set, then check_generic will not attempt to do solving if it encounters a
   generic. It will instead rely on direct equality.
   When calling a raw matcher, this must be supplied, as the default is to solve for
   generics. */
#define T_DONT_SOLVE 0x1

/* If this is set, then consider two types to be equivalent to each other if the right
   side provides more than the left side (something more derived).
   As of right now, only function returns make use of this. */
#define T_COVARIANT 0x2

/* If this is set, then consider two types to be equivalent to each other if the right
   side provides LESS than the left side (something less derived).
   As of right now, only function inputs use this. */
#define T_CONTRAVARIANT 0x4

/* Add the narrowest of the two matching types during match. */
#define T_UNIFY 0x8

lily_type_system *lily_new_type_system(lily_type_maker *tm)
{
    lily_type_system *ts = lily_malloc(sizeof(*ts));
    lily_type **types = lily_malloc(4 * sizeof(*types));

    ts->tm = tm;
    ts->types = types;
    ts->pos = 0;
    ts->max = 4;
    ts->max_seen = 1;
    ts->num_used = 0;
    ts->types[0] = lily_question_type;
    memset(ts->scoop_starts, 0, sizeof(ts->scoop_starts));

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
            sizeof(*ts->types) * ts->max);;
}

/* This is similar to lily_ts_resolve except that it also unrolls scoop types.
   Since scoop can unroll to 0 types (in the event it matches to nothing), this
   can't return a type. */
static void do_scoop_resolve(lily_type_system *ts, lily_type *type)
{
    if ((type->flags & (TYPE_IS_UNRESOLVED | TYPE_HAS_SCOOP)) == 0)
        lily_tm_add_unchecked(ts->tm, type);
    else if (type->cls->generic_count != 0) {
        lily_tm_reserve(ts->tm, type->subtype_count + 1 + ts->num_used);

        lily_type **subtypes = type->subtypes;
        int start = ts->tm->pos;
        int i = 0;

        for (;i < type->subtype_count;i++)
            do_scoop_resolve(ts, subtypes[i]);

        lily_type *t;

        if (type->cls->id == LILY_ID_FUNCTION)
            t = lily_tm_make_call(ts->tm, type->flags, type->cls,
                    ts->tm->pos - start);
        else
            t = lily_tm_make(ts->tm, type->cls, ts->tm->pos - start);

        lily_tm_add_unchecked(ts->tm, t);
    }
    else if (type->cls->id == LILY_ID_GENERIC)
        lily_tm_add_unchecked(ts->tm, ts->types[ts->pos + type->generic_pos]);
    else if (type->cls->id >= LOWEST_SCOOP_ID) {
        int scoop_pos = UINT16_MAX - type->cls->id;
        int stop = ts->scoop_starts[scoop_pos];
        int target = ts->scoop_starts[scoop_pos - 1];

        if (stop > target) {
            lily_tm_reserve(ts->tm, stop - target);

            /* Dump in whatever scoop collected. */
            for (;target < stop;target++)
                lily_tm_add_unchecked(ts->tm, ts->types[target]);
        }
    }
}

lily_type *lily_ts_scoop_unroll(lily_type_system *ts, lily_type *type)
{
    do_scoop_resolve(ts, type);
    return lily_tm_pop(ts->tm);
}

lily_type *lily_ts_resolve(lily_type_system *ts, lily_type *type)
{
    lily_type *ret = type;

    if ((type->flags & (TYPE_IS_UNRESOLVED | TYPE_HAS_SCOOP)) == 0)
        ;
    else if (type->cls->generic_count != 0) {
        /* Resolve handles solving generics and is thus hit pretty often. So
           it reserves the maximum that could possibly be used at once
           (including for scoops) to prevent repeated growing checks. */
        lily_tm_reserve(ts->tm, type->subtype_count + ts->num_used);
        lily_type **subtypes = type->subtypes;
        int start = ts->tm->pos;
        int i = 0;

        for (;i < type->subtype_count;i++)
            lily_tm_add_unchecked(ts->tm, lily_ts_resolve(ts, subtypes[i]));

        if (type->cls->id == LILY_ID_FUNCTION)
            ret = lily_tm_make_call(ts->tm, type->flags, type->cls,
                    ts->tm->pos - start);
        else
            ret = lily_tm_make(ts->tm, type->cls, ts->tm->pos - start);
    }
    else if (type->cls->id == LILY_ID_GENERIC)
        ret = ts->types[ts->pos + type->generic_pos];

    return ret;
}

static void unify_call(lily_type_system *ts, lily_type *left,
        lily_type *right, int num_subtypes)
{
    lily_class *cls = left->cls->id < right->cls->id ? left->cls : right->cls;
    int flags = (left->flags & TYPE_IS_VARARGS) &
                (right->flags & TYPE_IS_VARARGS);
    lily_tm_add(ts->tm, lily_tm_make_call(ts->tm, flags, cls, num_subtypes));
}

static void unify_simple(lily_type_system *ts, lily_type *left,
        lily_type *right, int num_subtypes)
{
    lily_class *cls = left->cls->id < right->cls->id ? left->cls : right->cls;

    if (num_subtypes)
        lily_tm_add(ts->tm, lily_tm_make(ts->tm, cls, num_subtypes));
    else
        lily_tm_add(ts->tm, cls->self_type);
}

static int check_raw(lily_type_system *, lily_type *, lily_type *, int);

static int check_generic(lily_type_system *ts, lily_type *left,
        lily_type *right, int flags)
{
    int ret;
    if (flags & T_DONT_SOLVE) {
        ret = (left == right);
        if (ret && flags & T_UNIFY)
            lily_tm_add(ts->tm, left);
    }
    else {
        int generic_pos = ts->pos + left->generic_pos;
        lily_type *cmp_type = ts->types[generic_pos];
        ret = 1;

        /* Scoop types can't be the solution, because generics need to be pinned
           down to one type (concrete or abstract). */
        if (right->cls->id == LILY_ID_SCOOP_1)
            ret = 0;
        else if (cmp_type == lily_question_type)
            ts->types[generic_pos] = right;
        else if (cmp_type == right)
            ;
        else if (cmp_type->flags & TYPE_IS_INCOMPLETE) {
            lily_type *unify_type;
            unify_type = lily_ts_unify(ts, cmp_type, right);
            if (unify_type)
                ts->types[generic_pos] = unify_type;
            else
                ret = 0;
        }
        else
            ret = check_raw(ts, cmp_type, right, flags | T_DONT_SOLVE);
    }

    return ret;
}

static int check_function(lily_type_system *ts, lily_type *left,
        lily_type *right, int flags)
{
    int ret = 1;
    int tm_start = lily_tm_pos(ts->tm);
    flags &= T_DONT_SOLVE | T_UNIFY;

    /* Remember that [0] is the return type, and always exists. */
    if (check_raw(ts, left->subtypes[0], right->subtypes[0], flags | T_COVARIANT) == 0) {
        if (flags & T_UNIFY) {
            lily_tm_restore(ts->tm, tm_start);
            lily_tm_add(ts->tm, lily_unit_type);
        }
    }

    int i;
    lily_type *left_type = NULL;
    lily_type *right_type = NULL;
    int count = left->subtype_count;

    if (left->subtype_count > right->subtype_count)
        count = right->subtype_count;

    flags |= T_CONTRAVARIANT;

    for (i = 1;i < count;i++) {
        left_type = left->subtypes[i];
        right_type = right->subtypes[i];

        if (right_type->cls->id == LILY_ID_OPTARG &&
            left_type->cls->id != LILY_ID_OPTARG) {
            right_type = right_type->subtypes[0];
        }

        if (check_raw(ts, left_type, right_type, flags) == 0) {
            ret = 0;
            break;
        }
    }

    if (right->subtype_count < left->subtype_count) {
        /* It could be that the left side is only a scoop type and the right
           side is empty. Setup the left side just in case. */
        if (left_type == NULL && left->subtype_count == 2)
            left_type = left->subtypes[1];

        ret = 0;
    }

    /* Allow it if the last type was a scoop type (and not trying to unify
       scoop types). */
    if (left_type &&
        left_type->cls->id >= LOWEST_SCOOP_ID &&
        (flags & T_UNIFY) == 0)
        ret = 1;
    else if (right->subtype_count > left->subtype_count &&
             right->subtypes[i]->cls->id != LILY_ID_OPTARG)
        ret = 0;

    if (ret && flags & T_UNIFY)
        unify_call(ts, left, right, left->subtype_count);

    return ret;
}

static int invariant_check(lily_type *left, lily_type *right, int *num_subtypes)
{
    int ret = left->cls == right->cls;
    *num_subtypes = left->subtype_count;

    return ret;
}

static int non_invariant_check(lily_type *left, lily_type *right, int *num_subtypes)
{
    int ret = lily_class_greater_eq(left->cls, right->cls);
    *num_subtypes = left->subtype_count;

    return ret;
}

static int check_misc(lily_type_system *ts, lily_type *left, lily_type *right,
        int flags)
{
    int ret;
    int num_subtypes;

    if (flags & T_COVARIANT)
        ret = non_invariant_check(left, right, &num_subtypes);
    else if (flags & T_CONTRAVARIANT)
        /* Contravariance is like covariance but with the sides in reverse
           order. So...call it like that. */
        ret = non_invariant_check(right, left, &num_subtypes);
    else
        ret = invariant_check(left, right, &num_subtypes);

    if (ret && num_subtypes) {
        /* This is really important. The caller's variance extends up to this class, but
           not into it. The caller may want contravariant matching, but the class may
           have its generics listed as being invariant.
           Proof:

           ```
           class Point() { ... }
           class Point3D() > Point() { ... }
           define f(in: list[Point3D]) { ... }
           define g(in: list[Point]) {
               in.append(Point())
           }

           # Type: list[Point3D]
           var v = [Point3D()]
           # After this, v[1] has type Point, but should be at least Point3D.
           g(v)

           ``` */
        flags &= T_DONT_SOLVE | T_UNIFY;

        ret = 1;

        lily_type **left_subtypes = left->subtypes;
        lily_type **right_subtypes = right->subtypes;
        int i;
        for (i = 0;i < num_subtypes;i++) {
            lily_type *left_entry = left_subtypes[i];
            lily_type *right_entry = right_subtypes[i];
            if (check_raw(ts, left_entry, right_entry, flags) == 0) {
                ret = 0;
                break;
            }
        }
    }

    if (ret && flags & T_UNIFY)
        unify_simple(ts, left, right, num_subtypes);

    return ret;
}

static int check_tuple(lily_type_system *ts, lily_type *left, lily_type *right,
        int flags)
{
    if (right->cls->id != LILY_ID_TUPLE)
        return 0;

    if ((left->flags & TYPE_HAS_SCOOP) == 0) {
        /* Do not allow Tuples to be considered equal if they don't have the
           same # of subtypes. The reason is that Tuple operations work on the
           size at vm-time. So if emit-time and vm-time disagree about the size
           of a Tuple, there WILL be problems. */
        if (left->subtype_count != right->subtype_count)
            return 0;
        else
            return check_misc(ts, left, right, flags);
    }

    /* Not yet. Maybe later. */
    if (flags & T_UNIFY)
        return 0;

    /* Scoop currently expects at least one value. */
    if (left->subtype_count > right->subtype_count)
        return 0;

    /* This carries a few assumptions with it:
     * Scoop types are always seen in order
     * Scoop types are never next to each other on the left
     * (Currently) Scoop types are always the only type. */
    int start = ts->pos + ts->num_used;
    int scoop_pos = UINT16_MAX - left->subtypes[0]->cls->id;

    ENSURE_TYPE_STACK(start + right->subtype_count)

    int i;
    for (i = 0;i < right->subtype_count;i++)
        ts->types[start + i] = right->subtypes[i];

    ts->num_used += right->subtype_count;
    ts->scoop_starts[scoop_pos] = ts->pos + ts->num_used;

    return 1;
}

static int collect_scoop(lily_type_system *ts, lily_type *left,
        lily_type *right, int flags)
{
    /* Not yet. Maybe later. */
    if (flags & T_UNIFY)
        return 0;

    int start = ts->pos + ts->num_used;
    int scoop_pos = UINT16_MAX - left->cls->id;

    ENSURE_TYPE_STACK(start + right->subtype_count)

    ts->types[start] = right;

    ts->num_used += 1;
    ts->scoop_starts[scoop_pos] = ts->pos + ts->num_used;

    return 1;
}

static int check_raw(lily_type_system *ts, lily_type *left, lily_type *right, int flags)
{
    int ret = 0;

    if (left->cls->id == LILY_ID_QUESTION) {
        ret = 1;
        if (flags & T_UNIFY)
            lily_tm_add(ts->tm, right);
    }
    else if (right->cls->id == LILY_ID_QUESTION) {
        ret = 1;
        if (flags & T_UNIFY)
            lily_tm_add(ts->tm, left);
    }
    else if (left->cls->id == LILY_ID_GENERIC)
        ret = check_generic(ts, left, right, flags);
    else if (left->cls->id == LILY_ID_FUNCTION &&
             right->cls->id == LILY_ID_FUNCTION)
        ret = check_function(ts, left, right, flags);
    else if (left->cls->id == LILY_ID_TUPLE)
        ret = check_tuple(ts, left, right, flags);
    else if (left->cls->id == LILY_ID_SCOOP_1)
        /* This is a match of a raw scoop versus a single argument. Consider
           this valid and, you know, scoop up the type. */
        ret = collect_scoop(ts, left, right, flags);
    else
        ret = check_misc(ts, left, right, flags);

    return ret;
}

int lily_ts_check(lily_type_system *ts, lily_type *left, lily_type *right)
{
    return check_raw(ts, left, right, T_COVARIANT);
}

lily_type *lily_ts_unify(lily_type_system *ts, lily_type *left, lily_type *right)
{
    int save_pos = ts->tm->pos;
    int ok = check_raw(ts, left, right, T_DONT_SOLVE | T_COVARIANT | T_UNIFY);
    lily_type *result;

    if (ok)
        result = lily_tm_pop(ts->tm);
    else {
        ts->tm->pos = save_pos;
        result = NULL;
    }

    return result;
}

int lily_ts_type_greater_eq(lily_type_system *ts, lily_type *left, lily_type *right)
{
    return check_raw(ts, left, right, T_DONT_SOLVE | T_COVARIANT);
}

lily_type *lily_ts_resolve_by_second(lily_type_system *ts, lily_type *first,
        lily_type *second)
{
    int stack_start = ts->pos + ts->num_used + 1;
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

void lily_ts_reset_scoops(lily_type_system *ts)
{
    memset(ts->scoop_starts, 0, 4 * sizeof(uint16_t));
}

#define COPY(to, from) \
to->pos = from->pos; \
to->num_used = from->num_used; \
memcpy(to->scoop_starts, from->scoop_starts, sizeof(to->scoop_starts));

void lily_ts_scope_save(lily_type_system *ts, lily_ts_save_point *p)
{
    COPY(p, ts)

    ts->pos += ts->num_used;
    ts->num_used = ts->max_seen;
    ts->scoop_starts[0] = ts->pos + ts->num_used;

    ENSURE_TYPE_STACK(ts->pos + ts->num_used);

    int i;
    for (i = 0;i < ts->num_used;i++)
        ts->types[ts->pos + i] = lily_question_type;
}

void lily_ts_scope_restore(lily_type_system *ts, lily_ts_save_point *p)
{
    COPY(ts, p)
}

void lily_ts_generics_seen(lily_type_system *ts, int amount)
{
    if (amount > ts->max_seen)
        ts->max_seen = amount;
}

int lily_func_type_num_optargs(lily_type *type)
{
    int i;
    for (i = type->subtype_count - 1;i > 0;i--) {
        lily_type *inner = type->subtypes[i];
        if (inner->cls->id != LILY_ID_OPTARG)
            break;
    }


    return type->subtype_count - i - 1;
}

int lily_class_greater_eq(lily_class *left, lily_class *right)
{
    int ret = 0;
    if (left != right) {
        while (right != NULL) {
            right = right->parent;
            if (right == left) {
                ret = 1;
                break;
            }
        }
    }
    else
        ret = 1;

    return ret;
}

int lily_class_greater_eq_id(int left_id, lily_class *right)
{
    int ret = 0;

    while (right != NULL) {
        if (right->id == left_id) {
            ret = 1;
            break;
        }

        right = right->parent;
    }

    return ret;
}
