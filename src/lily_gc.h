#ifndef LILY_GC_H
# define LILY_GC_H

# include "lily_core_types.h"

void lily_gc_collect_value(lily_mem_func, lily_type *, lily_raw_value);
void lily_gc_collect_any(lily_mem_func, lily_any_val *);
void lily_gc_collect_list(lily_mem_func, lily_type *, lily_list_val *);
void lily_gc_collect_hash(lily_mem_func, lily_type *, lily_hash_val *);
void lily_gc_collect_tuple(lily_mem_func, lily_type *, lily_list_val *);

#endif