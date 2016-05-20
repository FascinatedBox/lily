#include "lily_core_types.h"
#include "lily_vm.h"

#include "lily_api_alloc.h"
#include "lily_api_value_ops.h"

void lily_gc_function_marker(int pass, lily_value *v)
{
    lily_function_val *function_val = v->value.function;

    lily_value **upvalues = function_val->upvalues;
    int count = function_val->num_upvalues;
    int i;

    for (i = 0;i < count;i++) {
        lily_value *up = upvalues[i];
        /* If whatever this thing is can't be deref'd, then it either
            doesn't exist or doesn't have a marker function. */
        if (up && (up->flags & VAL_IS_GC_SWEEPABLE))
            lily_gc_mark(pass, up);
    }
}
