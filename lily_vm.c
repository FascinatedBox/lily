#include "lily_impl.h"
#include "lily_symtab.h"
#include "lily_opcode.h"

static void builtin_print(lily_symbol *s)
{
    if (s->val_type == vt_str)
        lily_impl_send_html(((lily_strval *)s->value)->str);
}

void lily_vm_execute(lily_symbol *sym)
{
    lily_symbol **regs;
    int *code, ci;

    regs = lily_malloc(8 * sizeof(lily_symbol *));
    code = sym->code_data->code;
    ci = 0;

    while (1) {
        switch (code[ci]) {
            case o_load_reg:
                regs[code[ci+1]] = (lily_symbol *)code[ci+2];
                ci += 3;
                break;
            case o_builtin_print:
                builtin_print(regs[code[ci+1]]);
                ci += 2;
                break;
            case o_assign:
                {
                    /* fixme: This will leak if the left side is a string or a
                       list. */
                    lily_symbol *left = (lily_symbol *)regs[code[ci+1]];
                    lily_symbol *right = (lily_symbol *)regs[code[ci+2]];
                    left->value = right->value;
                    left->val_type = right->val_type;
                }
                ci += 3;
                break;
            case o_vm_return:
                lily_free(regs);
                return;
        }
    }
}
