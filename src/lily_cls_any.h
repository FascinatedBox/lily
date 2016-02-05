#ifndef LILY_CLS_ANY_H
# define LILY_CLS_ANY_H

# include "lily_core_types.h"

lily_any_val *lily_new_any_val();
void lily_gc_any_marker(int, lily_value *);
void lily_gc_collect_any(lily_value *);
void lily_destroy_any(lily_value *);

#endif
