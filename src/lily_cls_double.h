#ifndef LILY_CLS_DOUBLE_H
#define LILY_CLS_DOUBLE_H

# include "lily_core_types.h"

int lily_double_eq(struct lily_vm_state_ *, int *, lily_value *, lily_value *);
int lily_double_setup(lily_symtab *, lily_class *);

#endif
