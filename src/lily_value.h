#ifndef LILY_VALUE_H
# define LILY_VALUE_H

# include "lily_core_types.h"

void lily_deref(lily_value *);
void lily_assign_value(lily_value *, lily_value *);
void lily_move(lily_value *, lily_raw_value, int);
lily_value *lily_copy_value(lily_value *);
int lily_eq_value(struct lily_vm_state_ *, lily_value *, lily_value *);

void lily_move(lily_value *, lily_raw_value, int);
void lily_move_boolean(lily_value *, int64_t);
void lily_move_closure(lily_value *, lily_function_val *);
void lily_move_double(lily_value *, double);
void lily_move_dynamic(lily_value *, lily_dynamic_val *);
void lily_move_enum(lily_value *, lily_instance_val *);
void lily_move_file(lily_value *, lily_file_val *);
void lily_move_foreign(lily_value *, lily_foreign_val *);
void lily_move_function(lily_value *, lily_function_val *);
void lily_move_hash(lily_value *, lily_hash_val *);
void lily_move_instance(lily_value *, lily_instance_val *);
void lily_move_instance_gc(lily_value *, lily_instance_val *);
void lily_move_integer(lily_value *, int64_t);
void lily_move_list(lily_value *, lily_list_val *);
void lily_move_shared_enum(lily_value *, lily_instance_val *);
void lily_move_string(lily_value *, lily_string_val *);

lily_value *lily_new_value(uint64_t, lily_raw_value);
lily_instance_val *lily_new_instance_val();
lily_instance_val *lily_new_enum_1(uint16_t, uint16_t, lily_value *);

#define lily_new_foreign(raw) \
lily_new_value(VAL_IS_FOREIGN | VAL_IS_DEREFABLE, (lily_raw_value){(lily_generic_val *)raw})

#define lily_new_list(raw) \
lily_new_value(VAL_IS_LIST | VAL_IS_DEREFABLE, (lily_raw_value){raw})

lily_value *lily_new_string(const char *);
lily_value *lily_new_string_take(char *);
lily_value *lily_new_string_ncpy(const char *, int);
lily_string_val *lily_new_raw_string(const char *);
lily_string_val *lily_new_raw_string_sized(const char *, int);

#endif
