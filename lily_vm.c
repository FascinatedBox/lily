#include "lily_impl.h"
#include "lily_symtab.h"
#include "lily_opcode.h"

static void builtin_print(lily_object *o)
{
    if (o->cls->id == SYM_CLASS_STR)
        lily_impl_send_html(((lily_strval *)o->value.ptr)->str);
}

void lily_vm_execute(lily_excep_data *error, lily_symbol *sym)
{
    lily_code_data *cd = sym->code_data;
    int *code, i, len;
    lily_object *left, *right;

    code = cd->code;
    len = cd->len;
    i = 0;

    while (i != len) {
        switch(code[i]) {
            case o_builtin_print:
                builtin_print((lily_object *)code[i+1]);
                i += 2;
                break;
            case o_assign:
                ((lily_object *)code[i+1])->value =
                ((lily_object *)code[i+2])->value;
                i += 3;
                break;
            case o_vm_return:
                return;
        }
    }
}
