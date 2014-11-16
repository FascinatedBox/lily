#ifndef LILY_GC_H
# define LILY_GC_H

# include "lily_core_types.h"

void lily_gc_collect_value(lily_sig *, lily_raw_value);
void lily_gc_collect_any(lily_any_val *);
void lily_gc_collect_list(lily_sig *, lily_list_val *);
void lily_gc_collect_hash(lily_sig *, lily_hash_val *);
void lily_gc_collect_tuple(lily_sig *, lily_list_val *);

#endif