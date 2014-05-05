#ifndef LILY_GC_H
# define LILY_GC_H

# include "lily_syminfo.h"

void lily_gc_collect_value(lily_sig *, lily_value);
void lily_gc_collect_object(lily_object_val *);
void lily_gc_collect_list(lily_sig *, lily_list_val *);
void lily_gc_collect_hash(lily_sig *, lily_hash_val *);

void lily_gc_list_marker(int, lily_sig *, lily_value);
void lily_gc_object_marker(int, lily_sig *, lily_value);
void lily_gc_hash_marker(int, lily_sig *, lily_value);

#endif