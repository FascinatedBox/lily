#include "lily_impl.h"
#include "lily_symtab.h"
#include "lily_opcode.h"

static char *typename(lily_object *o)
{
    char *ret = NULL;
    if (o->flags & OB_SYM)
        ret = "sym";
    else if (o->flags & OB_FIXED)
        ret = "fixed";

    return ret;
}

static void show_code(lily_symbol *sym)
{
    int i = 0;
    int len = sym->code_data->pos;
    int *code = sym->code_data->code;
    lily_object *left, *right;

    while (i < len) {
        switch (code[i]) {
            case o_builtin_print:
                left = ((lily_object *)code[i+1]);
                lily_impl_debugf("    [%d] builtin_print %s #%d\n",
                                 i, typename(left), left->id);
                i += 2;
                break;
            case o_assign:
                lily_impl_debugf("    [%d] assign        ", i);
                left = ((lily_object *)code[i+1]);
                right = ((lily_object *)code[i+2]);
                lily_impl_debugf("%s #%d to %s #%d\n", typename(left),
                                 left->id, typename(right), right->id);
                i += 3;
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
    lily_symbol *sym = symtab->sym_start;
    lily_object *obj = symtab->obj_start;

    /* The only classes now are the builtin ones. */
    lily_impl_debugf("Classes:\n");
    int class_i;
    for (class_i = 0;class_i <= SYM_CLASS_NUMBER;class_i++)
        lily_impl_debugf("#%d: (builtin) %s\n", class_i, symtab->classes[class_i]->name);

    lily_impl_debugf("Fixed values:\n");
    /* Show fixed and storage values first. */
    if (obj != NULL) {
        while (obj != NULL) {
            if (obj->flags & OB_FIXED) {
                char *fmt;
                lily_impl_debugf("#%d: ", obj->id);
                if (obj->cls->id == SYM_CLASS_STR) {
                    lily_impl_debugf("str(%-0.50s)\n",
                                     ((lily_strval *)obj->value.ptr)->str);
                }
                else if (obj->cls->id == SYM_CLASS_INTEGER)
                    lily_impl_debugf("integer(%d)\n", obj->value.integer);
                else if (obj->cls->id == SYM_CLASS_NUMBER)
                    lily_impl_debugf("number(%f)\n", obj->value.number);
            }
            obj = obj->next;
        }
    }

    lily_impl_debugf("Symbols:\n");
    while (sym != NULL) {
        lily_impl_debugf("#%d: ", sym->id, sym->name);
        if (sym->line_num == 0) {
            /* This is a builtin symbol. */
            lily_impl_debugf("(builtin) %s %s\n", sym->sym_class->name,
                             sym->name);
            if (isafunc(sym) && sym->code_data != NULL)
                show_code(sym);
        }
        else {
            lily_impl_debugf("%s %s @ line %d\n", sym->sym_class->name,
                             sym->name, sym->line_num);
        }
        sym = sym->next;
    }
}
