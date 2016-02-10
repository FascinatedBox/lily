# ifndef LILY_TYPE_SYSTEM_H
# define LILY_TYPE_SYSTEM_H

# include "lily_core_types.h"
# include "lily_type_maker.h"

/* lily_type_system is a container for type information that is used by
   different parts of the interpreter. As a bonus, a series of common
   type-related operations are also provided.
   The main user of this is the emitter. Each currently-invoked function stores
   the generics it uses starting at 'pos' and up to 'ceiling'. This is done so
   that different trees can pull generics that are in the process of being
   resolved.
   When an emitter function enters, pos is set to pos + ceiling, and a new
   ceiling is set (which may be 0 if there are no generics).
   The area from 'ceiling' to 'max' is considered fair game, and may be blasted
   by type operations. But...you wouldn't put anything there, would you? */

typedef struct {
    lily_type **types;
    uint16_t pos;
    uint16_t max;
    uint16_t ceiling;

    /* This is set to the maximum # of generics that have ever been visible at
       one time. To simplify things, the ceiling is raised by this much each
       time. */
    uint16_t max_seen;

    lily_type *dynamic_class_type;
    lily_type *question_class_type;
    lily_type_maker *tm;
} lily_type_system;

lily_type_system *lily_new_type_system(lily_type_maker *, lily_type *,
        lily_type *);

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

/* This is a simpler version of lily_ts check that does not validate types.
   Use it if you need to dig out type information that has already been
   verified. */
void lily_ts_pull_generics(lily_type_system *, lily_type *, lily_type *);

/* This recurses through the given type, building up a new, completely resolved
   type whereever the given type has generics.
   In the event that the given type specifies generics that are not solved,
   this function will solve them as Dynamic.
   The result is never NULL. */
lily_type *lily_ts_resolve(lily_type_system *, lily_type *);

lily_type *lily_ts_resolve_with(lily_type_system *, lily_type *, lily_type *);

/* This function is called when the first type (left) needs to be solved BUT
   the generics within left are not within the type stack.
   This situation happens when class member accesses wherein the class member
   has a generic type. In such a case, the left is the class instance (which
   provides all generic info), and the right is the type of the member.
   The result is a solved type, and never NULL. */
lily_type *lily_ts_resolve_by_second(lily_type_system *, lily_type *, lily_type *);

/* This is called to fill in generics directly, in the event that the first
   type is a variant and the second is the enum. The enum fills in the types
   that the variant wants (which may not be exactly [A, B, C], but could be
   [A, C]). */
void lily_ts_resolve_as_variant_by_enum(lily_type_system *, lily_type *, lily_type *);

/* This function marks every unresolved generic as solved by itself. This may
   seem silly, but there are at least two uses for this:
   * Function argument type mismatch, where some generics are not solved. Not
     doing this would cause the wrong types to get shown.
   * This prevents the generics from WITHIN a generic function (which are
     quasi-known) from being resolved to something when they can't be. */
void lily_ts_resolve_as_self(lily_type_system *, lily_type *);

/* This is called when there is an error. It replaces the NULL in unsolved
   generics with the ? type. */
void lily_ts_resolve_as_question(lily_type_system *);

/* If a type has been resolved as something but that resolution isn't complete,
   this replaces the resolution with something that does ? to Dynamic. This is
   called after evaluating arguments to make sure that the emitter does not
   create types with ? inside. */
void lily_ts_default_incomplete_solves(lily_type_system *);

/* This function is called by emitter when it is about to enter a call. The
   current ceiling is added to the stack's pos. The new ceiling is set to
   whatever ts->max_seen is. */
int lily_ts_raise_ceiling(lily_type_system *);

/* This function is called when a function has been called. The stack's pos is
   adjusted downward to where it was, and the ceiling is restored to the given
   value. */
void lily_ts_lower_ceiling(lily_type_system *, int);

/* Given an enum type (the first), determine if the second is a valid member of that
   enum. */
int lily_ts_enum_membership_check(lily_type_system *, lily_type *, lily_type *);

/* The parser calls this each time that generics are collected. This reports
   how many were collected. max_seen may or may not be updated. */
void lily_ts_generics_seen(lily_type_system *, int);

int lily_ts_count_unsolved(lily_type_system *);

/* Determine if the first class passed is either a base class or the same class
   as the second one. This doesn't take the ts because the information needed
   is within the classes themselves. */
int lily_class_greater_eq(lily_class *, lily_class *);

#endif
