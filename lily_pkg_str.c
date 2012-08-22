#include <stdio.h>
#include <string.h>

#include "lily_impl.h"
#include "lily_pkg.h"

void lily_str_concat(int num_args, lily_sym **args)
{
    lily_strval *ret, *arg1, *arg2;
    ret = ((lily_strval *)args[0]->value.ptr);
    arg1 = ((lily_strval *)args[1]->value.ptr);
    arg2 = ((lily_strval *)args[2]->value.ptr);

    int newsize = arg1->size + arg2->size + 1;
    /* It's null if [0] is a storage that's never been assigned to before. */
    if (ret == NULL) {
        ret = lily_malloc(sizeof(lily_strval));
        if (ret == NULL)
            return;
        ret->str = lily_malloc(sizeof(char) * newsize);
        ret->refcount = 1;
    }
    else if (ret->size < newsize) {
        char *newstr;
        newstr = realloc(ret->str, sizeof(char) * newsize);
        if (newstr == NULL) {
            lily_free(ret->str);
            lily_free(ret);
            return;
        }
        ret->str = newstr;
    }

    strcpy(ret->str, arg1->str);
    strcat(ret->str, arg2->str);

    ret->size = newsize;
    args[0]->value.ptr = ret;
}

static lily_func_seed concat = {"concat", 2, 0, lily_str_concat,
        {SYM_CLASS_STR, SYM_CLASS_STR, SYM_CLASS_STR}};

lily_func_seed *str_seeds[] = {&concat};
