#ifndef LILY_VALUE_H
# define LILY_VALUE_H

# include "lily_core_types.h"

void lily_deref(lily_value *);
void lily_deref_raw(lily_type *, lily_raw_value);

void lily_assign_value(lily_value *, lily_value *);

void lily_move(lily_value *, lily_raw_value, int);

lily_value *lily_copy_value(lily_value *);

lily_value *lily_new_value(uint64_t, lily_raw_value);
lily_instance_val *lily_new_instance_val();

void lily_gc_collect_value(lily_value *);
void lily_gc_collect_raw(lily_raw_value, int);

int lily_value_eq(struct lily_vm_state_ *, lily_value *, lily_value *);



#define lily_new_string(raw) \
lily_new_value(VAL_IS_STRING | VAL_IS_DEREFABLE, (lily_raw_value)raw);

#define lily_new_list(raw) \
lily_new_value(VAL_IS_LIST | VAL_IS_DEREFABLE, (lily_raw_value)raw);



#define lily_move_any(target, raw) \
lily_move(target, (lily_raw_value)raw, VAL_IS_ANY | VAL_IS_DEREFABLE)

#define lily_move_boolean(target, raw) \
lily_move(target, (lily_raw_value){.integer = raw}, VAL_IS_BOOLEAN)

#define lily_move_double(target, raw) \
lily_move(target, (lily_raw_value)raw, VAL_IS_DOUBLE)

#define lily_move_enum(target, raw) \
lily_move(target, (lily_raw_value)raw, VAL_IS_ENUM | VAL_IS_DEREFABLE)

#define lily_move_file(target, raw) \
lily_move(target, (lily_raw_value)raw, VAL_IS_FILE | VAL_IS_DEREFABLE)

#define lily_move_function(target, raw) \
lily_move(target, (lily_raw_value)raw, VAL_IS_FUNCTION | VAL_IS_DEREFABLE)

#define lily_move_hash(target, raw) \
lily_move(target, (lily_raw_value)raw, VAL_IS_HASH | VAL_IS_DEREFABLE)

#define lily_move_instance(target, raw) \
lily_move(target, (lily_raw_value)raw, VAL_IS_INSTANCE | VAL_IS_DEREFABLE)

#define lily_move_integer(target, raw) \
lily_move(target, (lily_raw_value){.integer = raw}, VAL_IS_INTEGER)

#define lily_move_list(target, raw) \
lily_move(target, (lily_raw_value)raw, VAL_IS_LIST | VAL_IS_DEREFABLE)

#define lily_move_shared_enum(target, raw) \
lily_move(target, (lily_raw_value)raw, VAL_IS_ENUM)

#define lily_move_string(target, raw) \
lily_move(target, (lily_raw_value)raw, VAL_IS_STRING | VAL_IS_DEREFABLE)

#endif
