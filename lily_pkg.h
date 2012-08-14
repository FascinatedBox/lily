#ifndef LILY_PKG_H

# include "lily_symtab.h"

typedef void (*lily_fast_func)(lily_sym **);

typedef const struct {
    char *name;
    int num_args;
    int is_varargs;
    lily_fast_func func;
    int arg_ids[];
} lily_func_seed;

#endif
