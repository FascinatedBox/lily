#include <stdio.h>
#include <string.h>

#include "lily_impl.h"
#include "lily_pkg.h"

void lily_str_concat(int num_args, lily_sym **args)
{
    lily_str_val *ret, *arg1, *arg2;
    ret = args[0]->value.str;
    arg1 = args[1]->value.str;
    arg2 = args[2]->value.str;

    int newsize = arg1->size + arg2->size + 1;
    /* It's null if [0] is a storage that's never been assigned to before. */
    if (ret == NULL) {
        ret = lily_malloc(sizeof(lily_str_val));
        if (ret == NULL)
            return;

        ret->str = lily_malloc(sizeof(char) * newsize);
        if (ret->str == NULL) {
            lily_free(ret);
            return;
        }
        ret->refcount = 1;
    }
    else if (ret->size < newsize) {
        char *newstr;
        newstr = lily_realloc(ret->str, sizeof(char) * newsize);
        if (newstr == NULL)
            return;

        ret->str = newstr;
    }

    strcpy(ret->str, arg1->str);
    strcat(ret->str, arg2->str);

    ret->size = newsize;
    args[0]->value.str = ret;
    args[0]->flags &= ~S_IS_NIL;
}

static lily_func_seed concat = {"concat", 2, 0, lily_str_concat,
        {SYM_CLASS_STR, SYM_CLASS_STR, SYM_CLASS_STR}};

lily_func_seed *str_seeds[] = {&concat};
