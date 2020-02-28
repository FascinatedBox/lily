# ifndef LILY_TYPE_SYSTEM_H
# define LILY_TYPE_SYSTEM_H

# include "lily_core_types.h"
# include "lily_type_maker.h"

/* This is where the brains of Lily's type system lies. This handles type
   matching, unification, and solving.
   The type system (ts for short) works by having parser always tell it the max
   number of generics that any one function or class needs at once.
   How it works is by establishing a series of scope. Each scope has the max
   number of generics seen reserved into it. Matching will lay values down into
   the current scope, and will handle generics as it goes along. Resolving then
   later uses those filled-in types. */

typedef struct {
    uint16_t pos;
    uint16_t num_used;
    uint16_t scoop_count;
    uint16_t pad;
} lily_ts_save_point;

typedef struct {
    lily_type **types;
    lily_type **base;
    uint16_t pos;
    uint16_t num_used;
    uint16_t max_seen;
    uint16_t max;

    uint16_t scoop_count;
    uint16_t pad1;
    uint32_t pad2;

    lily_type_maker *tm;
} lily_type_system;

lily_type_system *lily_new_type_system(lily_type_maker *);
void lily_rewind_type_system(lily_type_system *);
void lily_free_type_system(lily_type_system *);

/* The first type (left) is the type that is wanted.
   The second type (right) is the type that is given.
   Determine if the two types match. Additionally, if 'left' provides generics,
   and those generics are unresolved, then right's types solve those generics. */
int lily_ts_check(lily_type_system *, lily_type *, lily_type *);

lily_type *lily_ts_unify(lily_type_system *, lily_type *, lily_type *);

/* This uses the type system to determine if the first type can be assigned
   to the second type. This understands variance, but will not solve any generics. */
int lily_ts_type_greater_eq(lily_type_system *, lily_type *, lily_type *);

/* This recurses through the given type, building up a new, completely resolved
   type whereever the given type has generics.
   In the event that the given type specifies generics that are not solved,
   this function will solve them as ?.
   The result is never NULL. */
lily_type *lily_ts_resolve(lily_type_system *, lily_type *);

/* This function is called when the first type (left) needs to be solved BUT
   the generics within left are not within the type stack.
   This situation happens when class member accesses wherein the class member
   has a generic type. In such a case, the left is the class instance (which
   provides all generic info), and the right is the type of the member.
   The result is a solved type, and never NULL. */
lily_type *lily_ts_resolve_by_second(lily_type_system *, lily_type *, lily_type *);

/* This does what lily_ts_resolve does as well as replacing the result of scoop
   if neccessary. Most callers don't want this and should use the regular
   resolve instead. */
lily_type *lily_ts_resolve_unscoop(lily_type_system *, lily_type *);

/* This saves information for the current scope down to the save point, and
   reserves a fresh set of types for a new scope. */
void lily_ts_scope_save(lily_type_system *, lily_ts_save_point *);

/* This restores ts to a previously-established scope. */
void lily_ts_scope_restore(lily_type_system *, lily_ts_save_point *);

/* The parser calls this each time that generics are collected. This reports
   how many were collected. max_seen may or may not be updated. */
void lily_ts_generics_seen(lily_type_system *, int);

/* Determine if the first class passed is either a base class or the same class
   as the second one. This doesn't take the ts because the information needed
   is within the classes themselves. */
int lily_class_greater_eq(lily_class *, lily_class *);

/* The same as lily_class_greater_eq, except that an id is passed in place of a
   first class. */
int lily_class_greater_eq_id(int, lily_class *);

#endif
