#include "lily_impl.h"
#include "lily_symtab.h"
#include "lily_opcode.h"

static void builtin_print(lily_sym *sym)
{
    if (sym->cls->id == SYM_CLASS_STR)
        lily_impl_send_html(((lily_strval *)sym->value.ptr)->str);
}

void lily_vm_execute(lily_excep_data *error, lily_var *var)
{
    lily_code_data *cd = var->code_data;
    int *code, i, len;

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
                ((lily_sym *)code[i+1])->value =
                ((lily_sym *)code[i+2])->value;
                i += 3;
                break;
            case o_vm_return:
                return;
        }
    }
}
