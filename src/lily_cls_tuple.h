#ifndef LILY_CLS_TUPLE_H
# define LILY_CLS_TUPLE_H

# include "lily_core_types.h"

int lily_tuple_eq(struct lily_vm_state_ *, int *, lily_value *, lily_value *);
void lily_gc_tuple_marker(int, lily_value *);
void lily_destroy_tuple(lily_value *);
void lily_gc_collect_tuple(lily_type *, lily_list_val *);

#endif
