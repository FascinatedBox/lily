#ifndef LILY_CLS_BYTESTRING_H
#define LILY_CLS_BYTESTRING_H

# include "lily_core_types.h"

int lily_bytestring_eq(struct lily_vm_state_ *, int *, lily_value *, lily_value *);
int lily_bytestring_setup(lily_symtab *, lily_class *);

#endif
