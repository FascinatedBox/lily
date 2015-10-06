# ifndef LILY_TYPE_SYSTEM_H
# define LILY_TYPE_SYSTEM_H
# include "lily_core_types.h"
# include "lily_raiser.h"
# include "lily_symtab.h"

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

    lily_symtab *symtab;
} lily_type_system;

lily_type_system *lily_new_type_system(lily_symtab *);

void lily_free_type_system(lily_type_system *);

/* The first type (left) is the type that is wanted.
   The second type (right) is the type that is given.
   Determine if the two types match. Additionally, if 'left' provides generics,
   and those generics are unresolved, then right's types solve those generics. */
int lily_ts_check(lily_type_system *, lily_type *, lily_type *);

/* This uses the type system to determine if the first type can be assigned
   to the second type. This understands variance, but will not solve any generics. */
int lily_ts_type_greater_eq(lily_type_system *, lily_type *, lily_type *);

/* This is a simpler version of lily_ts check that does not validate types.
   Use it if you need to dig out type information that has already been
   verified. */
void lily_ts_pull_generics(lily_type_system *, lily_type *, lily_type *);

/* Given a type that IS a generic (not one that contains them), determine what
   that generic has been resolved as.
   Note: May return NULL if the generic has not really been resolved. */
lily_type *lily_ts_easy_resolve(lily_type_system *, lily_type *);

/* This recurses through the given type, building up a new, completely resolved
   type whereever the given type has generics.
   In the event that the given type specifies generics that are not solved,
   this function will solve them as 'any'.
   The result is never NULL. */
lily_type *lily_ts_resolve(lily_type_system *, lily_type *);

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
void lily_ts_resolve_as_self(lily_type_system *);

/* This ensures that there are enough empty slots beyond ts->pos and ts->ceiling
   to place 'N' types. This is used when building tuples. */
void lily_ts_reserve_ceiling_types(lily_type_system *, int);

/* Set a type to the stack, at 'pos + ceiling + (index)'.
   Since the type is set after the ceiling, this will not harm the current
   resolution status of anything. However, other functions (particularly the
   resolving ones), will cheerfully overwrite the types here.
   This is suitable for short-term stashing only. */
void lily_ts_set_ceiling_type(lily_type_system *, lily_type *, int);

/* Retrieve a type from past the ceiling. */
lily_type *lily_ts_get_ceiling_type(lily_type_system *, int);

/* Occasionally, it is useful to store types in the ceiling of the stack, then
   build a type from those ceiling types.
   An example of this is emitter's hash build: The key is stored at
   ceiling + 0, and the value at ceiling + 1. This is then called with class
   hash and '2' to build a hash of two ceiling types.
   The index given is the top index, relative to the ceiling. */
lily_type *lily_ts_build_by_ceiling(lily_type_system *, lily_class *, int, int);

/* Given a variant type, build an appropriate enum type. If the variant type
   supplies any generics, then those are used. Generics that are in the enum
   but not the variant will default to any. */
lily_type *lily_ts_build_enum_by_variant(lily_type_system *, lily_type *);

/* This function is called by emitter when it is about to enter a call. The
   current ceiling is added to the stack's pos. The new ceiling is set to
   whatever ts->max_seen is. */
int lily_ts_raise_ceiling(lily_type_system *);

/* This function is called when a function has been called. The stack's pos is
   adjusted downward to where it was, and the ceiling is restored to the given
   value. */
void lily_ts_lower_ceiling(lily_type_system *, int);

/* Given an enum type (the first), determine if the second is a valid member of that
   enum. The type 'any' should not be passed to this, as it is not a true enum. */
int lily_ts_enum_membership_check(lily_type_system *, lily_type *, lily_type *);

/* Return a count of how many types, from ts->pos to ts->ceiling, are
   unresolved. This function is used by the emitter to make sure that lambda
   arguments have all of their type information (and that resolution isn't
   defaulting to any in some places). */
int lily_ts_count_unresolved(lily_type_system *);

/* The parser calls this each time that generics are collected. This reports
   how many were collected. max_seen may or may not be updated. */
void lily_ts_generics_seen(lily_type_system *, int);

/* Determine if the first class passed is either a base class or the same class
   as the second one. This doesn't take the ts because the information needed
   is within the classes themselves. */
int lily_class_greater_eq(lily_class *, lily_class *);

#endif
