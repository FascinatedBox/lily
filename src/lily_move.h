#ifndef LILY_MOVE_H
# define LILY_MOVE_H

# include "lily_value_structs.h"

void lily_move_boolean(lily_value *, int64_t);
void lily_move_bytestring(lily_value *, lily_string_val *);
void lily_move_double(lily_value *, double);
void lily_move_dynamic(lily_value *, lily_dynamic_val *);
void lily_move_empty_variant(lily_value *, lily_instance_val *);
void lily_move_enum_f(uint32_t, lily_value *, lily_instance_val *);
void lily_move_file(lily_value *, lily_file_val *);
void lily_move_foreign_f(uint32_t, lily_value *, lily_foreign_val *);
void lily_move_function_f(uint32_t, lily_value *, lily_function_val *);
void lily_move_hash_f(uint32_t, lily_value *, lily_hash_val *);
void lily_move_instance_f(uint32_t, lily_value *, lily_instance_val *);
void lily_move_integer(lily_value *, int64_t);
void lily_move_list_f(uint32_t, lily_value *, lily_list_val *);
void lily_move_string(lily_value *, lily_string_val *);
void lily_move_tuple_f(uint32_t, lily_value *, lily_list_val *);
void lily_move_unit(lily_value *);

/* This value can be ref'd/deref'd, but does not contain any tagged data inside
   of it. */
#define MOVE_DEREF_NO_GC          VAL_IS_DEREFABLE
/* This value can be ref'd/deref'd, and may contain tagged data inside. Use this
   if you're not sure (such as with a `List[A]`. */
#define MOVE_DEREF_SPECULATIVE    (VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE)
/* This value should not be ref'd/deref'd. This is typically for the default
   value of a variant. Use this if you're certain that the type of the value
   has instances where it could have sweepable data. Examples of this include
   `None` for values like `List[Option[Integer]]`, because `Some(Integer)`
   cannot have circular data inside. */
#define MOVE_SHARED_NO_GC         0
/* This value should not be ref'd/deref'd. This is the more common case for
   default values of a variant. Use this if you're not sure that the type might
   have sweepable data. `List[Option[A]]` is an example where the `None` is not
   interesting, but might be replaced with `Some(Dynamic)` which needs to be
   swept through. */
#define MOVE_SHARED_SPECULATIVE   (VAL_IS_GC_SPECULATIVE)

#endif
