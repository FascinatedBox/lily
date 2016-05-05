#ifndef LILY_CLS_DYNAMIC_H
# define LILY_CLS_DYNAMIC_H

# include "lily_core_types.h"

lily_dynamic_val *lily_new_dynamic_val();
void lily_gc_dynamic_marker(int, lily_value *);
void lily_destroy_dynamic(lily_value *);

#endif
