#ifndef LILY_CLS_FUNCTION_H
# define LILY_CLS_FUNCTION_H

# include "lily_core_types.h"

lily_function_val *lily_new_native_function_val(char *, char *);
lily_function_val *lily_new_foreign_function_val(lily_foreign_func,
        char *, char *);
lily_function_val *lily_new_function_copy(lily_function_val *);
void lily_gc_function_marker(int, lily_value *);
void lily_destroy_function(lily_value *);
void lily_gc_collect_function(lily_type *, lily_function_val *);
#endif
