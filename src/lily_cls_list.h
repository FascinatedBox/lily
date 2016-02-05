#ifndef LILY_CLS_LIST_H
#define LILY_CLS_LIST_H

# include "lily_core_types.h"

lily_list_val *lily_new_list_val();
void lily_gc_list_marker(int, lily_value *);
void lily_destroy_list(lily_value *);
void lily_gc_collect_list(lily_value *);
lily_class *lily_list_init(lily_symtab *);

#endif
