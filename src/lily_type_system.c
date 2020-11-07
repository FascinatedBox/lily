#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lily.h"
#include "lily_alloc.h"
#include "lily_type_system.h"

extern lily_class *lily_self_class;
extern lily_type *lily_scoop_type;
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
    ts->base = types;
    ts->pos = 0;
    ts->max = 4;
    ts->max_seen = 1;
    ts->num_used = 0;
    ts->types[0] = lily_question_type;
    ts->scoop_count = 0;

    return ts;
}

void lily_rewind_type_system(lily_type_system *ts)
{
    ts->base = ts->types;
    ts->num_used = 0;
    ts->pos = 0;
    ts->scoop_count = 0;
}

void lily_free_type_system(lily_type_system *ts)
{
    lily_free(ts->types);
    lily_free(ts);
}

static void grow_types(lily_type_system *ts)
{
    ptrdiff_t offset = ts->base - ts->types;
    ts->max *= 2;
    ts->types = lily_realloc(ts->types, sizeof(*ts->types) * ts->max);
    ts->base = ts->types + offset;
}

/* This is similar to lily_ts_resolve except that it also unrolls scoop types.
   Since scoop can unroll to 0 types (in the event it matches to nothing), this
   can't return a type. */
static void do_scoop_resolve(lily_type_system *ts, lily_type *type)
{
    if ((type->flags & (TYPE_IS_UNRESOLVED | TYPE_HAS_SCOOP)) == 0)
        lily_tm_add_unchecked(ts->tm, type);
    else if (type->subtype_count) {
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
        lily_tm_add_unchecked(ts->tm, ts->base[type->generic_pos]);
    else if (type == lily_scoop_type) {
        int i;
        lily_type **base = ts->base + ts->num_used - ts->scoop_count;
        lily_tm_reserve(ts->tm, ts->scoop_count);

        for (i = 0;i < ts->scoop_count;i++)
            lily_tm_add_unchecked(ts->tm, base[i]);
    }
}

lily_type *lily_ts_resolve_unscoop(lily_type_system *ts, lily_type *type)
{
    do_scoop_resolve(ts, type);
    return lily_tm_pop(ts->tm);
}

lily_type *lily_ts_resolve(lily_type_system *ts, lily_type *type)
{
    lily_type *ret = type;

    if ((type->flags & (TYPE_IS_UNRESOLVED | TYPE_HAS_SCOOP)) == 0)
        ;
    else if (type->subtype_count) {
        /* Resolve handles solving generics and is thus hit pretty often. So
           it reserves the maximum that could possibly be used at once to
           prevent repeated growing checks. */
        lily_tm_reserve(ts->tm, type->subtype_count);
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
        ret = ts->base[type->generic_pos];

    return ret;
}

static void unify_call(lily_type_system *ts, lily_type *left,
        lily_type *right, int num_subtypes)
{
    lily_class *cls = left->cls;
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
        int generic_pos = left->generic_pos;
        lily_type *cmp_type = ts->base[generic_pos];
        ret = 1;

        if (cmp_type->flags & TYPE_IS_INCOMPLETE) {
            lily_type *unify_type;
            unify_type = lily_ts_unify(ts, cmp_type, right);
            if (unify_type)
                ts->base[generic_pos] = unify_type;
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
    /* The return type is at [0] and always exists. */
    lily_type *left_type = left->subtypes[0];
    lily_type *right_type = right->subtypes[0];

    flags &= T_DONT_SOLVE | T_UNIFY;

    if (check_raw(ts, left_type, right_type, flags | T_COVARIANT) == 0) {
        /* If the goal is to unify, then any two mismatched types can always
           narrow down to `Unit`. */
        if (flags & T_UNIFY) {
            lily_tm_restore(ts->tm, tm_start);
            lily_tm_add(ts->tm, lily_unit_type);
        }
        /* Only narrow to `Unit` if it's the requirement. This works because the
           result can be ignored.
           This does not apply to the right side. */
        else if (left_type == lily_unit_type)
            ;
        else
            ret = 0;
    }

    int i;
    int count = left->subtype_count;

    if (count > right->subtype_count) {
        /* Not enough types. This is fine if the unmatched type is scoop (which
           should always be last). */
        if (left->subtypes[right->subtype_count] == lily_scoop_type)
            ;
        else
            ret = 0;

        count = right->subtype_count;
    }
    else if (right->subtype_count > count) {
        /* Too many types. This is fine if they're either going to scoop, or
           they're optional. */
        if (left->subtypes[count-1] == lily_scoop_type)
            ;
        else if (right->subtypes[count]->cls->id == LILY_ID_OPTARG)
            ;
        else
            ret = 0;
    }

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
    /* Tuples of different sizes are considered distinct in case that's
       important later on. This check is also important because Tuple is the
       only non-Function type of varying arity. */
    if (left->subtype_count != right->subtype_count)
        return 0;

    return check_misc(ts, left, right, flags);
}

static int collect_scoop(lily_type_system *ts, lily_type *left,
        lily_type *right, int flags)
{
    (void)left;

    /* Not yet. Maybe later. */
    if (flags & T_UNIFY)
        return 0;

    ENSURE_TYPE_STACK(ts->pos + ts->num_used + 1)

    ts->base[ts->num_used] = right;

    ts->num_used += 1;
    ts->scoop_count += 1;

    return 1;
}

static int check_raw(lily_type_system *ts, lily_type *left, lily_type *right, int flags)
{
    int ret = 0;

    if (left->cls->id == LILY_ID_QUESTION) {
        /* Scoop is only valid if it's a requirement. It can't be allowed to
           unify, because it breaks the type system. */
        if (right != lily_scoop_type && right->cls != lily_self_class) {
            ret = 1;
            if (flags & T_UNIFY)
                lily_tm_add(ts->tm, right);
        }
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
    else if (left == lily_scoop_type)
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
    /* This is used to resolve properties and variants that have enums. The
       resolve source shouldn't be the current scope, but instead 'first' which
       is the class or enum self type. Rather than creating a new scope, a much
       easier way is to swap out the base pointer that acts as the floor of the
       current scope. */
    lily_type **save_base = ts->base;
    ts->base = first->subtypes;
    lily_type *result_type = lily_ts_resolve(ts, second);
    ts->base = save_base;

    return result_type;
}

#define COPY(to, from) \
to->pos = from->pos; \
to->num_used = from->num_used; \
to->scoop_count = from->scoop_count;

void lily_ts_scope_save(lily_type_system *ts, lily_ts_save_point *p)
{
    COPY(p, ts)

    ts->base += ts->num_used;
    ts->pos += ts->num_used;
    ts->num_used = ts->max_seen;
    ts->scoop_count = 0;

    ENSURE_TYPE_STACK(ts->pos + ts->num_used);

    int i;
    for (i = 0;i < ts->num_used;i++)
        ts->base[i] = lily_question_type;
}

void lily_ts_scope_restore(lily_type_system *ts, lily_ts_save_point *p)
{
    COPY(ts, p)
    ts->base -= ts->num_used;
}

void lily_ts_generics_seen(lily_type_system *ts, uint16_t amount)
{
    if (amount > ts->max_seen)
        ts->max_seen = amount;
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
