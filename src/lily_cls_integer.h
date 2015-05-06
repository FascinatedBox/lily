#ifndef LILY_CLS_INTEGER_H
#define LILY_CLS_INTEGER_H

# include "lily_core_types.h"

int lily_integer_eq(struct lily_vm_state_ *, int *, lily_value *, lily_value *);
int lily_integer_setup(lily_symtab *, lily_class *);

#endif
