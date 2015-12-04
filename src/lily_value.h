#ifndef LILY_VALUE_H
# define LILY_VALUE_H

# include "lily_core_types.h"

void lily_deref(lily_value *);
void lily_deref_raw(lily_type *, lily_raw_value);

void lily_assign_value(lily_value *, lily_value *);
void lily_move_raw_value(lily_value *, lily_raw_value);
lily_value *lily_copy_value(lily_value *);

lily_value *lily_new_value(uint64_t, lily_type *, lily_raw_value);
lily_instance_val *lily_new_instance_val();
lily_instance_val *lily_new_instance_val_for(lily_type *);

void lily_gc_collect_value(lily_type *, lily_raw_value);

int lily_generic_eq(struct lily_vm_state_ *, int *, lily_value *, lily_value *);
int lily_instance_eq(struct lily_vm_state_ *, int *, lily_value *, lily_value *);

#endif
