#include "lily_impl.h"
#include "lily_symtab.h"
#include "lily_opcode.h"

static void builtin_print(lily_symbol *s)
{
    if (s->sym_class->id == SYM_CLASS_STR)
        lily_impl_send_html(((lily_strval *)s->value)->str);
}

void lily_vm_execute(lily_excep_data *error, lily_symbol *sym)
{
    lily_symbol **regs;
    int *code, ci;

    regs = lily_malloc(8 * sizeof(lily_symbol *));
    if (regs == NULL)
        lily_raise_nomem(error);

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
                    left->sym_class = right->sym_class;
                }
                ci += 3;
                break;
            case o_vm_return:
                lily_free(regs);
                return;
        }
    }
}
