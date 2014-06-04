#include <ctype.h>
#include <string.h>

#include "lily_impl.h"
#include "lily_value.h"
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
    if ((ret_reg->flags & VAL_IS_NIL_OR_PROTECTED) ||
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

        if ((ret_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
            ret_reg->value.generic->refcount--;

        ret = new_sv;
        ret_reg->flags &= ~VAL_IS_PROTECTED;
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
    vm_regs[code[2]]->flags &= ~VAL_IS_NIL;
}

#define CTYPE_WRAP(WRAP_NAME, WRAPPED_CALL) \
void WRAP_NAME(lily_vm_state *vm, uintptr_t *code, int num_args) \
{ \
    lily_value **vm_regs = vm->vm_regs; \
    lily_value *ret_arg = vm_regs[code[1]]; \
    lily_value *input_arg = vm_regs[code[0]]; \
\
    if (input_arg->flags & VAL_IS_NIL || \
        input_arg->value.str->size == 0) { \
        ret_arg->value.integer = 0; \
        ret_arg->flags = 0; \
        return; \
    } \
\
    char *loop_str = input_arg->value.str->str; \
    int i = 0; \
\
    ret_arg->value.integer = 1; \
    ret_arg->flags = 0; \
    for (i = 0;i < input_arg->value.str->size;i++) { \
        if (WRAPPED_CALL(loop_str[i]) == 0) { \
            ret_arg->value.integer = 0; \
            break; \
        } \
    } \
}

CTYPE_WRAP(lily_str_isdigit, isdigit)
CTYPE_WRAP(lily_str_isalpha, isalpha)
CTYPE_WRAP(lily_str_isspace, isspace)
CTYPE_WRAP(lily_str_isalnum, isalnum)

static const lily_func_seed isalnum_fn =
    {"isalnum", lily_str_isalnum, NULL,
        {SYM_CLASS_FUNCTION, 2, 0, SYM_CLASS_INTEGER, SYM_CLASS_STR}};

static const lily_func_seed isdigit_fn =
    {"isdigit", lily_str_isdigit, &isalnum_fn,
        {SYM_CLASS_FUNCTION, 2, 0, SYM_CLASS_INTEGER, SYM_CLASS_STR}};

static const lily_func_seed isalpha_fn =
    {"isalpha", lily_str_isalpha, &isdigit_fn,
        {SYM_CLASS_FUNCTION, 2, 0, SYM_CLASS_INTEGER, SYM_CLASS_STR}};

static const lily_func_seed isspace_fn =
    {"isspace", lily_str_isspace, &isalpha_fn,
        {SYM_CLASS_FUNCTION, 2, 0, SYM_CLASS_INTEGER, SYM_CLASS_STR}};

static const lily_func_seed concat =
    {"concat", lily_str_concat, &isspace_fn,
        {SYM_CLASS_FUNCTION, 3, 0, SYM_CLASS_STR, SYM_CLASS_STR, SYM_CLASS_STR}};

#define SEED_START concat

int lily_str_setup(lily_class *cls)
{
    cls->seed_table = &SEED_START;
    return 1;
}
