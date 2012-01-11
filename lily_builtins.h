#ifndef LILY_BUILTINS_H
# define LILY_BUILTINS_H

# include "lily_symtab.h"

struct lily_sym_t;

typedef void (*lily_fast_func)(struct lily_sym_t **);

void lily_builtin_print(struct lily_sym_t **);

#endif
