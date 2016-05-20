#ifndef LILY_CLS_LIST_H
#define LILY_CLS_LIST_H

# include "lily_core_types.h"

void lily_gc_list_marker(int, lily_value *);
lily_class *lily_list_init(lily_symtab *);

#endif
