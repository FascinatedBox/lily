#include <inttypes.h>
#include <string.h>

#include "lily_impl.h"
#include "lily_symtab.h"
#include "lily_opcode.h"

/** Debug is responsible for:
    * Pretty printing out the code for all methods that have been scanned,
      notably @main.

    Debug starts by being given a symtab, and then walking over it to determine
    what methods to print out, then printing them out. This is extremely useful
    for seeing what the vm is seeing.

    This is also nice because one can opt to not show the symtab, and then this
    will be skipped entirely. **/

/* Opcode printing is handled by getting a 'call info' array for the given
   opcode. This specifies values that start with D_ that indicate how to handle
   printing the particular opcode. New values can be added, but must be
   documented.
   These codes specify what is at a particular position after an opcode.
   If an op has a line number, an input, and a result, then the code would be
   D_LINENO, D_INPUT, D_OUTPUT. (This is for unary ops). */

/* D_LINENO: This position contains the line number upon which the opcode is
             executed. If this exists, it is always right after the opcode. */
#define D_LINENO     0

/* D_INPUT:      This specifies a symbol that is being read. */
#define D_INPUT      1
/* D_OUTPUT:     The opcode's result will be written to this place. */
#define D_OUTPUT     2
/* D_NOP:        This position does not do anything. */
#define D_NOP        3
/* D_JUMP:       This position contains a jump to a future location. The
                 position is an int, not a sym. */
#define D_JUMP       4
/* D_JUMP_ON:    This contains 1 or 0. This is used to determine if o_jump_if
                 should jump on truth or false value. */
#define D_JUMP_ON    5
/* D_COUNT:      This specifies a number of arguments or values to come. This
                 value is stored but not shown. */
#define D_COUNT      8
/* D_COUNT_LIST: This specifies the start of an argument list, using the value
                 recorded by D_COUNT. */
#define D_COUNT_LIST 9
/* D_SHOW_COUNT: This specifies a number of arguments or values that may be seen
                 in the future. This value is printed next to the opcode name.
                 save and restore use this. */
#define D_SHOW_COUNT 10
/* D_IGNORE:     This specifies a value that should be ignored. This is used to
                 skip over o_build_list's sig, which isn't necessary because the
                 output's sig is shown. */
#define D_IGNORE     11

char *opcode_names[33] = {
    "assign",
    "object assign",
    "str assign",
    "list assign",
    "subscript assign",
    "integer add (+)",
    "number add (+)",
    "integer minus (-)",
    "number minus (-)",
    "is equal (==)",
    "less (<)",
    "less equal (<=)",
    "greater (>)",
    "greater equal (>=)",
    "not equal (!=)",
    "integer multiply (*)",
    "number multiply (*)",
    "integer divide (/)",
    "number divide (/)",
    "jump",
    "jump if",
    "function call",
    "method call",
    "return value",
    "return",
    "save",
    "restore",
    "unary not (!x)",
    "unary minus (-x)",
    "build list",
    "subscript",
    "typecast",
    "return from vm"
};

static const int call_ci[]       =
    {5, D_LINENO, D_INPUT, D_COUNT, D_OUTPUT, D_COUNT_LIST};
static const int build_list_ci[] =
    {5, D_LINENO, D_OUTPUT, D_COUNT, D_IGNORE, D_COUNT_LIST};
static const int sub_assign_ci[] = {4, D_LINENO, D_INPUT, D_INPUT, D_INPUT};
static const int binary_ci[]     = {4, D_LINENO, D_INPUT, D_INPUT, D_OUTPUT};
static const int unary_ci[]      = {3, D_LINENO, D_INPUT, D_OUTPUT};
static const int assign_ci[]     = {3, D_LINENO, D_OUTPUT, D_INPUT};
static const int jump_if_ci[]    = {3, D_JUMP_ON, D_INPUT, D_JUMP};
static const int save_ci[]       = {2, D_SHOW_COUNT, D_COUNT_LIST};
static const int return_ci[]     = {1, D_OUTPUT};
static const int restore_ci[]    = {1, D_SHOW_COUNT};
static const int jump_ci[]       = {1, D_JUMP};
static const int nop_ci[]        = {1, D_NOP};

static void show_sig(lily_sig *sig)
{
    lily_impl_debugf(sig->cls->name);

    if (sig->cls->id == SYM_CLASS_METHOD ||
        sig->cls->id == SYM_CLASS_FUNCTION) {
        lily_call_sig *csig = sig->node.call;
        lily_impl_debugf(" (");
        int i;
        for (i = 0;i < csig->num_args-1;i++) {
            show_sig(csig->args[i]);
            lily_impl_debugf(", ");
        }
        if (i != csig->num_args) {
            show_sig(csig->args[i]);
            if (csig->is_varargs)
                lily_impl_debugf("...");
        }
        lily_impl_debugf("):");
        if (csig->ret == NULL)
            lily_impl_debugf("nil");
        else
            show_sig(csig->ret);
    }
    else if (sig->cls->id == SYM_CLASS_LIST) {
        lily_impl_debugf("[");
        show_sig(sig->node.value_sig);
        lily_impl_debugf("]");
    }
}

static const int *code_info_for_opcode(int opcode)
{
    const int *ret;

    switch (opcode) {
        case o_assign:
        case o_list_assign:
        case o_str_assign:
        case o_obj_assign:
        case o_obj_typecast:
            ret = assign_ci;
            break;
        case o_integer_add:
        case o_number_add:
        case o_integer_minus:
        case o_number_minus:
        case o_is_equal:
        case o_less:
        case o_less_eq:
        case o_greater:
        case o_greater_eq:
        case o_not_eq:
        case o_integer_mul:
        case o_number_mul:
        case o_integer_div:
        case o_number_div:
        case o_subscript:
            ret = binary_ci;
            break;
        case o_method_call:
        case o_func_call:
            ret = call_ci;
            break;
        case o_return_noval:
        case o_vm_return:
            ret = nop_ci;
            break;
        case o_unary_not:
        case o_unary_minus:
            ret = unary_ci;
            break;
        case o_sub_assign:
            ret = sub_assign_ci;
            break;
        case o_build_list:
            ret = build_list_ci;
            break;
        case o_save:
            ret = save_ci;
            break;
        case o_jump:
            ret = jump_ci;
            break;
        case o_restore:
            ret = restore_ci;
            break;
        case o_return_val:
            ret = return_ci;
            break;
        case o_jump_if:
            ret = jump_if_ci;
            break;
        default:
            lily_impl_debugf("warning: Opcode %d has no ci.\n", opcode);
            ret = NULL;
            break;
    }

    return ret;
}

static void show_literal(lily_sym *sym)
{
    int cls_id = sym->sig->cls->id;

    if (cls_id == SYM_CLASS_STR)
        lily_impl_debugf("(%-0.50s)\n", sym->value.str->str);
    else if (cls_id == SYM_CLASS_INTEGER)
        lily_impl_debugf("(%" PRId64 ")\n", sym->value.integer);
    else if (cls_id == SYM_CLASS_NUMBER)
        lily_impl_debugf("(%f)\n", sym->value.number);
}

static void show_code_sym(lily_sym *sym)
{
    lily_impl_debugf("(");
    show_sig(sym->sig);
    lily_impl_debugf(") ");

    if (sym->flags & VAR_SYM) {
        lily_var *var = (lily_var *)sym;
        lily_impl_debugf("var %s (from line %d).\n", var->name,
                var->line_num);
    }
    else if (sym->flags & LITERAL_SYM) {
        lily_impl_debugf("literal ");
        show_literal(sym);
    }
    else if (sym->flags & STORAGE_SYM)
        lily_impl_debugf("storage at %p.\n", sym);
}

/* show_code
   This shows all the code in a var that holds a method. This displays the
   operations in a simple way so that users can get an idea of what the vm is
   seeing.
   This function uses call info (ci) to determine how to handle given opcodes.
   This was done because many opcodes (all binary ones, for example), share the
   same basic structure. This also eliminates the need for specialized
   functions. */
static void show_code(lily_var *var)
{
    int i, len;
    uintptr_t *code;
    lily_method_val *m;

    m = var->value.method;
    i = 0;
    code = m->code;
    len = m->pos;

    while (i < len) {
        int opcode = code[i];
        const int *opcode_data = code_info_for_opcode(opcode);
        char *opcode_name = opcode_names[opcode];
        int count, data_code, j;

        if (i != 0)
            fputs("|\n", stderr);

        lily_impl_debugf("|____ [%d] %s", i, opcode_name);
        /* A newline isn't printed after the opcode's name so that the line
           number can be on the same line. Most opcodes have a line number,
           except for a few where that does not apply.
           Most that do not have a special starting opcode specific to them that
           will write in the newline.
           These two do not, so write it in for them. */
        if (code[i] == o_jump || code[i] == o_return_val)
            lily_impl_debugf("\n");

        for (j = 0;j < opcode_data[0];j++) {
            data_code = opcode_data[j+1];
            if (data_code == D_LINENO)
                lily_impl_debugf("   (at line %d)\n", (int)code[i+j+1]);
            else if (data_code == D_INPUT) {
                lily_impl_debugf("|     <---- ");
                show_code_sym((lily_sym *)code[i+j+1]);
            }
            else if (data_code == D_OUTPUT) {
                /* output is NULL if it's a method or function that does not
                   return a value. Omit this for brevity (the lack of a stated
                   output meaning it doesn't have one). */
                if ((lily_sym *)code[i+j+1] != NULL) {
                    lily_impl_debugf("|     ====> ");
                    show_code_sym((lily_sym *)code[i+j+1]);
                }
            }
            else if (data_code == D_JUMP_ON) {
                if (code[i+1+j] == 0)
                    lily_impl_debugf(" false\n");
                else
                    lily_impl_debugf(" true\n");
            }
            else if (data_code == D_JUMP)
                lily_impl_debugf("|     -> | [%d]\n", (int)code[i+j+1]);
            else if (data_code == D_COUNT)
                count = (int)code[i+j+1];
            else if (data_code == D_COUNT_LIST) {
                if (count == 0)
                    i--;
                else {
                    int k;
                    for (k = 0;k < count;k++, j++) {
                        lily_impl_debugf("|     <---- ");
                        show_code_sym((lily_sym *)code[i+j+1]);
                    }
                    j--;
                }
            }
            else if (data_code == D_SHOW_COUNT) {
                count = (int)code[i+j+1];
                lily_impl_debugf(" (%d)\n", count);
            }
            else if (data_code == D_NOP) {
                lily_impl_debugf("\n");
                break;
            }
        }
        i += j + 1;
    }
}

/** API for lily_debug.c **/

/* lily_show_symtab
   This is the API function for debugging. Just send the symtab and debug will
   do the rest. */
void lily_show_symtab(lily_symtab *symtab)
{
    lily_var *var = symtab->var_start;
    lily_literal *lit = symtab->lit_start;

    lily_impl_debugf("Literals:\n");
    /* Show literal values first. */
    while (lit != NULL) {
        lily_impl_debugf("#%d: ", lit->id);
        show_literal((lily_sym *)lit);
        lit = lit->next;
    }

    /* Now, give information about all of the methods that have code assigned to
       them. Methods that are arguments get scoped out, and are thus ignored
       since they do not have code. */
    lily_impl_debugf("Methods:\n");
    while (var != NULL) {
        if (var->sig->cls->id == SYM_CLASS_METHOD) {
            lily_impl_debugf("method %s @ line %d\n", var->name, var->line_num);
            show_code(var);
        }
        var = var->next;
    }
}
