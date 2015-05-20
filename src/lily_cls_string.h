#ifndef LILY_CLS_STRING_H
#define LILY_CLS_STRING_H

# include "lily_core_types.h"

int lily_string_eq(struct lily_vm_state_ *, int *, lily_value *, lily_value *);
lily_class *lily_string_init(lily_symtab *);
void lily_destroy_string(lily_value *);

#endif
