#include <stdio.h>
#include <string.h>

#include "lily_impl.h"
#include "lily_pkg.h"
#include "lily_vm.h"

void lily_str_concat(lily_vm_state *vm, uintptr_t *code, int num_args)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_str_val *ret, *arg1, *arg2;
    lily_value *ret_reg;
    ret_reg = vm_regs[code[2]];
    ret = vm_regs[code[2]]->value.str;
    arg1 = vm_regs[code[0]]->value.str;
    arg2 = vm_regs[code[1]]->value.str;

    int newsize = arg1->size + arg2->size + 1;

        /* Create a str if there isn't one. */
    if ((ret_reg->flags & SYM_IS_NIL) ||
        /* ...or to preserve immutability. */
        ret == arg1 || ret == arg2) {
        lily_str_val *new_sv = lily_malloc(sizeof(lily_str_val));
        char *new_str = lily_malloc(sizeof(char) * newsize);
        if (new_sv == NULL || new_str == NULL) {
            lily_free(new_sv);
            lily_free(new_str);
            return;
        }

        new_sv->str = new_str;
        new_sv->refcount = 1;
        new_sv->size = newsize;

        strcpy(new_sv->str, arg1->str);
        strcat(new_sv->str, arg2->str);

        if ((ret_reg->flags & SYM_IS_NIL) == 0)
            ret_reg->value.generic->refcount--;

        ret = new_sv;
    }
    else if (ret->size < newsize) {
        char *newstr;
        newstr = lily_realloc(ret->str, sizeof(char) * newsize);
        if (newstr == NULL)
            return;

        ret->str = newstr;
        strcpy(ret->str, arg1->str);
        strcat(ret->str, arg2->str);
    }

    vm_regs[code[2]]->value.str = ret;
    vm_regs[code[2]]->flags &= ~SYM_IS_NIL;
}

static lily_func_seed concat =
    {"concat", lily_str_concat,
        {SYM_CLASS_FUNCTION, 3, 0, SYM_CLASS_STR, SYM_CLASS_STR, SYM_CLASS_STR}};

lily_func_seed *str_seeds[] = {&concat};
