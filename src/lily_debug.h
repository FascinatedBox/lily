#ifndef LILY_DEBUG_H
# define LILY_DEBUG_H

# include "lily_symtab.h"

void lily_show_sym(lily_vm_state *vm, lily_function_val *, lily_function_val *,
		lily_value *, int, int, lily_msgbuf *);

#endif
