#include "lily_impl.h"
#include "lily_symtab.h"
#include "lily_opcode.h"

#define FAST_INTEGER_OP(OP) \
((lily_sym *)code[i+3])->value.integer = \
((lily_sym *)code[i+1])->value.integer OP \
((lily_sym *)code[i+2])->value.integer; \
i += 4; \

#define NUMBER_OP(OP) \
lhs = (lily_sym *)code[i+1]; \
if (lhs->sig->cls->id == SYM_CLASS_NUMBER) { \
    rhs = (lily_sym *)code[i+2]; \
    if (rhs->sig->cls->id == SYM_CLASS_NUMBER) \
        ((lily_sym *)code[i+3])->value.number = \
        lhs->value.number OP rhs->value.number; \
    else \
        ((lily_sym *)code[i+3])->value.number = \
        lhs->value.number OP rhs->value.integer; \
} \
else \
    ((lily_sym *)code[i+3])->value.number = \
    lhs->value.integer OP ((lily_sym *)code[i+2])->value.number; \
i += 4;

void lily_builtin_print(lily_sym **args)
{
    lily_impl_send_html(((lily_strval *)args[0]->value.ptr)->str);
}

void lily_vm_execute(lily_excep_data *error, lily_var *var)
{
    lily_func_prop *fp = var->properties;
    int *code, i, len;
    lily_sym *lhs, *rhs;

    code = fp->code;
    len = fp->len;
    i = 0;

    while (i != len) {
        switch(code[i]) {
            case o_assign:
                lhs = ((lily_sym *)code[i+1]);
                lhs->flags &= ~S_IS_NIL;
                lhs->value = ((lily_sym *)code[i+2])->value;
                i += 3;
                break;
            case o_integer_add:
                FAST_INTEGER_OP(+)
                break;
            case o_integer_minus:
                FAST_INTEGER_OP(-)
                break;
            case o_number_add:
                NUMBER_OP(+)
                break;
            case o_number_minus:
                NUMBER_OP(-)
                break;
            case o_func_call:
            {
                /* var, func, #args, args... */
                lily_fast_func fc = (lily_fast_func)code[i+2];
                int j = code[i+3];
                fc((lily_sym **)code+i+4);
                i += 4+j;
            }
            case o_vm_return:
                return;
        }
    }
}
