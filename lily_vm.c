#include <string.h>

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

#define COMPARE_OP(OP, STROP) \
lhs = (lily_sym *)code[i+1]; \
rhs = (lily_sym *)code[i+2]; \
if (lhs->sig->cls->id == SYM_CLASS_NUMBER) { \
    if (rhs->sig->cls->id == SYM_CLASS_NUMBER) \
        ((lily_sym *)code[i+3])->value.number = \
        lhs->value.number OP rhs->value.number; \
    else \
        ((lily_sym *)code[i+3])->value.integer = \
        lhs->value.number OP rhs->value.integer; \
} \
else if (lhs->sig->cls->id == SYM_CLASS_INTEGER) { \
    if (rhs->sig->cls->id == SYM_CLASS_INTEGER) \
        ((lily_sym *)code[i+3])->value.integer =  \
        lhs->value.integer OP rhs->value.number; \
    else \
        ((lily_sym *)code[i+3])->value.integer = \
        lhs->value.number OP rhs->value.number; \
} \
else if (lhs->sig->cls->id == SYM_CLASS_STR) { \
    ((lily_sym *)code[i+3])->value.integer = \
    strcmp(((lily_strval *)lhs->value.ptr)->str, \
           ((lily_strval *)rhs->value.ptr)->str) STROP; \
} \
i += 4;

void lily_builtin_print(lily_sym **args)
{
    lily_impl_send_html(((lily_strval *)args[0]->value.ptr)->str);
}

void lily_vm_execute(lily_excep_data *error, lily_var *var)
{
    int *code, i, len;
    lily_sym *lhs, *rhs;
    lily_method_val *m;

    m = (lily_method_val *)var->value.ptr;
    code = m->code;
    len = m->len;
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
            case o_less:
                COMPARE_OP(<, == -1)
                break;
            case o_less_eq:
                COMPARE_OP(<=, <= 0)
                break;
            case o_is_equal:
                COMPARE_OP(>, == 0)
                break;
            case o_greater:
                COMPARE_OP(>, == 1)
                break;
            case o_greater_eq:
                COMPARE_OP(>, >= 0)
                break;
            case o_func_call:
            {
                /* var, func, #args, args... */
                lily_fast_func fc = (lily_fast_func)code[i+2];
                int j = code[i+3];
                fc((lily_sym **)code+i+4);
                i += 4+j;
            }
                break;
            case o_obj_assign:
                lhs = ((lily_sym *)code[i+1]);
                lhs->flags &= ~S_IS_NIL;
                lhs->value = ((lily_sym *)code[i+2])->value;
                lhs->sig->node.value_sig = ((lily_sym *)code[i+2])->sig;
                i += 3;
                break;
            case o_vm_return:
                return;
        }
    }
}
