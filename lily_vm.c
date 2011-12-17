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
if (lhs->cls->id == SYM_CLASS_NUMBER) { \
    rhs = (lily_sym *)code[i+2]; \
    if (rhs->cls->id == SYM_CLASS_NUMBER) \
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

static void builtin_print(lily_sym *sym)
{
    if (sym->cls->id == SYM_CLASS_STR)
        lily_impl_send_html(((lily_strval *)sym->value.ptr)->str);
}

void lily_vm_execute(lily_excep_data *error, lily_var *var)
{
    lily_code_data *cd = var->code_data;
    int *code, i, len;
    lily_sym *lhs, *rhs;

    code = cd->code;
    len = cd->len;
    i = 0;

    while (i != len) {
        switch(code[i]) {
            case o_builtin_print:
                builtin_print((lily_sym *)code[i+1]);
                i += 2;
                break;
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
            case o_vm_return:
                return;
        }
    }
}
