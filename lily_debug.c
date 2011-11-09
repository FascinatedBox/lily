#include "lily_impl.h"
#include "lily_symtab.h"
#include "lily_opcode.h"

static char *typename(lily_sym *sym)
{
    char *ret;

    if (sym->flags & VAR_SYM)
        ret = "var";
    else if (sym->flags & LITERAL_SYM)
        ret = "literal";
    else if (sym->flags & STORAGE_SYM)
        ret = "storage";
    else
        ret = NULL;

    return ret;
}

static void show_code(lily_var *var)
{
    int i = 0;
    int len = var->code_data->pos;
    int *code = var->code_data->code;
    lily_sym *left, *right, *result;

    while (i < len) {
        switch (code[i]) {
            case o_builtin_print:
                left = ((lily_sym *)code[i+1]);
                lily_impl_debugf("    [%d] builtin_print %s #%d\n",
                                 i, typename((lily_sym *)left), left->id);
                i += 2;
                break;
            case o_assign:
                lily_impl_debugf("    [%d] assign        ", i);
                left = ((lily_sym *)code[i+1]);
                right = ((lily_sym *)code[i+2]);
                lily_impl_debugf("%s #%d to %s #%d\n",
                                 typename((lily_sym *)left),
                                 left->id, typename((lily_sym *)right),
                                 right->id);
                i += 3;
                break;
            case o_integer_add:
                lily_impl_debugf("    [%d] integer_add   ", i);
                left = ((lily_sym *)code[i+1]);
                right = ((lily_sym *)code[i+2]);
                result = ((lily_sym *)code[i+3]);
                lily_impl_debugf("%s #%d + %s #%d to %s #%d\n",
                                 typename((lily_sym *)left),
                                 left->id, typename((lily_sym *)right),
                                 right->id, typename((lily_sym *)result),
                                 result->id);
                i += 4;
                break;
            case o_vm_return:
                lily_impl_debugf("    [%d] vm_return\n", i);
                return;
            default:
                lily_impl_debugf("    [%d] bad opcode %d.\n", i, code[i]);
                return;
        }
    }
}

void lily_show_symtab(lily_symtab *symtab)
{
    lily_var *var = symtab->var_start;
    lily_literal *lit = symtab->lit_start;

    /* The only classes now are the builtin ones. */
    lily_impl_debugf("Classes:\n");
    int class_i;
    for (class_i = 0;class_i <= SYM_CLASS_NUMBER;class_i++)
        lily_impl_debugf("#%d: (builtin) %s\n", class_i, symtab->classes[class_i]->name);

    lily_impl_debugf("Literals:\n");
    /* Show literal values first. */
    while (lit != NULL) {
        lily_impl_debugf("#%d: ", lit->id);
        if (lit->cls->id == SYM_CLASS_STR) {
            lily_impl_debugf("str(%-0.50s)\n",
                             ((lily_strval *)lit->value.ptr)->str);
        }
        else if (lit->cls->id == SYM_CLASS_INTEGER)
            lily_impl_debugf("integer(%d)\n", lit->value.integer);
        else if (lit->cls->id == SYM_CLASS_NUMBER)
            lily_impl_debugf("number(%f)\n", lit->value.number);
        lit = lit->next;
    }

    lily_impl_debugf("Vars:\n");
    while (var != NULL) {
        lily_impl_debugf("#%d: ", var->id);
        if (var->line_num == 0) {
            /* This is a builtin symbol. */
            lily_impl_debugf("(builtin) %s %s\n", var->cls->name,
                             var->name);
            if (isafunc(var) && var->code_data != NULL)
                show_code(var);
        }
        else {
            lily_impl_debugf("%s %s @ line %d\n", var->cls->name,
                             var->name, var->line_num);
        }
        var = var->next;
    }
}
