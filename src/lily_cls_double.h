#ifndef LILY_CLS_DOUBLE_H
#define LILY_CLS_DOUBLE_H

# include "lily_core_types.h"

int lily_double_eq(struct lily_vm_state_ *, int *, lily_value *, lily_value *);
lily_class *lily_double_init(lily_symtab *);

#endif
