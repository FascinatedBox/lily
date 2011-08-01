#include "lily_impl.h"
#include "lily_symtab.h"
#include "lily_opcode.h"

static void builtin_print(lily_symbol *s)
{
    if (s->val_type == vt_str)
        lily_impl_send_html(((lily_strval *)s->sym_value)->str);
}

void lily_vm_execute(lily_symbol *sym)
{
    lily_symbol **regs;
    int *code, ci;

    regs = lily_impl_malloc(8 * sizeof(lily_symbol *));
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
            case o_vm_return:
                free(regs);
                return;
        }
    }
}
