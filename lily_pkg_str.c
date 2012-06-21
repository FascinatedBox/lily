#include "lily_pkg.h"

void lily_str_concat(lily_sym **args)
{

}

static lily_func_seed concat = {"concat", 1, lily_str_concat,
        {SYM_CLASS_STR, SYM_CLASS_STR}};

lily_func_seed *str_seeds[] = {&concat};
