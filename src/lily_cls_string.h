#ifndef LILY_CLS_STRING_H
#define LILY_CLS_STRING_H

# include "lily_core_types.h"

lily_class *lily_string_init(lily_symtab *);
void lily_string_subscript(lily_vm_state *, lily_value *, lily_value *,
        lily_value *);

#endif
