#ifndef LILY_BUILTINS_H
# define LILY_BUILTINS_H

# include "lily_symtab.h"

typedef void (*lily_fast_func)(lily_sym **);

void lily_builtin_print(lily_sym **);

#endif
