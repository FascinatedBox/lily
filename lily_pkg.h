#ifndef LILY_PKG_H

# include "lily_symtab.h"
# include "lily_vm.h"

typedef const struct {
    char *name;
    lily_func func;
    int arg_ids[];
} lily_func_seed;

#endif
