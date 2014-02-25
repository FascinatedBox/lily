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

/* D_LINENO:        This position contains the line number upon which the opcode
                    is executed. If this exists, it is always right after the
                    opcode. */
#define D_LINENO         0
/* D_INPUT:         This specifies a symbol that is being read. */
#define D_INPUT          1
/* D_OUTPUT:        The opcode's result will be written to this place. */
#define D_OUTPUT         2
/* D_NOP:           This position does not do anything. */
#define D_NOP            3
/* D_JUMP:          This position contains a jump to a future location. The
                    position is an int, not a sym. */
#define D_JUMP           4
/* D_JUMP_ON:       This contains 1 or 0. This is used to determine if o_jump_if
                    should jump on truth or false value. */
#define D_JUMP_ON        5
/* D_COUNT:         This specifies a number of arguments or values to come. This
                    value is stored but not shown. */
#define D_COUNT          8
/* D_COUNT_LIST:    This specifies the start of an argument list, using the
                    value recorded by D_COUNT. */
#define D_COUNT_LIST     9
/* D_INT_VAL:       Show a value that's just an integer. This is used by
                    o_for_setup to determine if it should init the step value or
                    not. */
#define D_INT_VAL       10
/* D_LIT_INPUT:     The input is the address of a literal. The literal's value
                    is shown. */
#define D_LIT_INPUT     11
/* D_GLOBAL_INPUT:  The input is the address of a global (@main) register. */
#define D_GLOBAL_INPUT  12
/* D_GLOBAL_OUTPUT: The output is the address of a global (@main) register. */
#define D_GLOBAL_OUTPUT 13
/* D_IS_GLOBAL:     This specifies if an upcoming value is a global or a local.
                    This is used by show, where the value might be a global var,
                    or a local one. */
#define D_IS_GLOBAL     14
/* D_SHOW_INPUT:    A value given to show. This can be either a global or a
                    local register. */
#define D_SHOW_INPUT    15

/* Opcodes that have line numbers also have extra space so they print the line
   number at an even spot. This saves debug from having to calculate how much
   (and possibly getting it wrong) at the cost of a little bit of memory.
   No extra space means it doesn't have a line number. */
char *opcode_names[44] = {
    "assign",
    "object assign",
    "assign (ref/deref)",
    "subscript assign",
    "integer add (+)",
    "integer minus (-)",
    "modulo (%)",
    "integer multiply (*)",
    "integer divide (/)",
    "left shift (<<)",
    "right shift (>>)",
    "bitwise and (a & b)",
    "bitwise or (a | b)",
    "bitwise xor (a ^ b)",
    "number add (+)",
    "number minus (-)",
    "number multiply (*)",
    "number divide (/)",
    "is equal (==)",
    "not equal (!=)",
    "less (<)",
    "less equal (<=)",
    "greater (>)",
    "greater equal (>=)",
    "jump",
    "jump if",
    "function call",
    "method call",
    "return value",
    "return (no value)",
    "unary not (!x)",
    "unary minus (-x)",
    "build list",
    "subscript",
    "typecast",
    "integer <-> number",
    "show",
    "return expected",
    "for (integer range)",
    "for setup",
    "get global",
    "set global",
    "get const",
    "return from vm"
};

static const int for_setup_ci[] =
    {6, D_LINENO, D_INPUT, D_INPUT, D_INPUT, D_INPUT, D_INT_VAL};
static const int for_integer_ci[] =
    {6, D_LINENO, D_INPUT, D_INPUT, D_INPUT, D_INPUT, D_JUMP};
static const int call_ci[]        =
    {5, D_LINENO, D_INPUT, D_COUNT, D_COUNT_LIST, D_OUTPUT};
static const int build_list_ci[]  =
    {4, D_LINENO, D_COUNT, D_COUNT_LIST, D_OUTPUT};
static const int sub_assign_ci[] = {4, D_LINENO, D_INPUT, D_INPUT, D_INPUT};
static const int binary_ci[]     = {4, D_LINENO, D_INPUT, D_INPUT, D_OUTPUT};
static const int get_const_ci[]  = {3, D_LINENO, D_LIT_INPUT, D_OUTPUT};
static const int get_global_ci[] = {3, D_LINENO, D_GLOBAL_INPUT, D_OUTPUT};
static const int set_global_ci[] = {3, D_LINENO, D_INPUT, D_GLOBAL_OUTPUT};
static const int in_out_ci[]     = {3, D_LINENO, D_INPUT, D_OUTPUT};
static const int jump_if_ci[]    = {3, D_JUMP_ON, D_INPUT, D_JUMP};
static const int intnum_ci[]     = {3, D_LINENO, D_INPUT, D_OUTPUT};
static const int show_ci[]       = {3, D_LINENO, D_IS_GLOBAL, D_SHOW_INPUT};
static const int return_ci[]     = {2, D_LINENO, D_INPUT};
static const int return_nv_ci[]  = {1, D_LINENO};
static const int jump_ci[]       = {1, D_JUMP};
static const int nop_ci[]        = {1, D_NOP};

static void show_sig(lily_sig *sig, char *name)
{
    lily_impl_debugf(sig->cls->name);

    if (sig->cls->id == SYM_CLASS_METHOD ||
        sig->cls->id == SYM_CLASS_FUNCTION) {
        if (name != NULL)
            lily_impl_debugf(" %s", name);

        lily_impl_debugf(" (");
        if (sig->siglist[1] != NULL) {
            int i;
            for (i = 1;i < sig->siglist_size - 1;i++) {
                show_sig(sig->siglist[i], NULL);
                lily_impl_debugf(", ");
            }
            show_sig(sig->siglist[i], NULL);
            if (sig->flags & SIG_IS_VARARGS)
                lily_impl_debugf("...");
        }

        lily_impl_debugf("):");
        if (sig->siglist[0] == NULL)
            lily_impl_debugf("nil");
        else
            show_sig(sig->siglist[0], NULL);
    }
    else if (sig->cls->id == SYM_CLASS_LIST) {
        lily_impl_debugf("[");
        show_sig(sig->siglist[0], NULL);
        lily_impl_debugf("]");
    }
}

static const int *code_info_for_opcode(int opcode)
{
    const int *ret;

    switch (opcode) {
        case o_assign:
        case o_ref_assign:
        case o_obj_assign:
        case o_obj_typecast:
        case o_unary_not:
        case o_unary_minus:
            ret = in_out_ci;
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
        case o_modulo:
        case o_integer_mul:
        case o_number_mul:
        case o_integer_div:
        case o_number_div:
        case o_subscript:
        case o_left_shift:
        case o_right_shift:
        case o_bitwise_and:
        case o_bitwise_or:
        case o_bitwise_xor:
            ret = binary_ci;
            break;
        case o_method_call:
        case o_func_call:
            ret = call_ci;
            break;
        case o_return_from_vm:
            ret = nop_ci;
            break;
        case o_return_expected:
        case o_return_noval:
            ret = return_nv_ci;
            break;
        case o_sub_assign:
            ret = sub_assign_ci;
            break;
        case o_build_list:
            ret = build_list_ci;
            break;
        case o_jump:
            ret = jump_ci;
            break;
        case o_return_val:
            ret = return_ci;
            break;
        case o_show:
            ret = show_ci;
            break;
        case o_jump_if:
            ret = jump_if_ci;
            break;
        case o_intnum_typecast:
            ret = intnum_ci;
            break;
        case o_integer_for:
            ret = for_integer_ci;
            break;
        case o_for_setup:
            ret = for_setup_ci;
            break;
        case o_get_const:
            ret = get_const_ci;
            break;
        case o_set_global:
            ret = set_global_ci;
            break;
        case o_get_global:
            ret = get_global_ci;
            break;
        default:
            lily_impl_debugf("warning: Opcode %d has no ci.\n", opcode);
            ret = NULL;
            break;
    }

    return ret;
}

/* show_str
   This displays a literal string in a safe way. It iterates over a string and
   packs the data into a msgbuf as it goes along. */
static void show_str(char *str, lily_msgbuf *msgbuf)
{
    char escape_char;
    int i, len, start;

    len = strlen(str);
    if (len > 64)
        len = 64;

    lily_msgbuf_add(msgbuf, "\"");
    for (i = 0, start = 0;i < len;i++) {
        char ch = str[i];

        if (ch == '\n')
            escape_char = 'n';
        else if (ch == '\r')
            escape_char = 'r';
        else if (ch == '\t')
            escape_char = 't';
        else if (ch == '\'')
            escape_char = '\'';
        else if (ch == '"')
            escape_char = '"';
        else if (ch == '\\')
            escape_char = '\\';
        else if (ch == '\b')
            escape_char = 'b';
        else if (ch == '\a')
            escape_char = 'a';
        else
            escape_char = 0;

        if (escape_char) {
            if (i != start)
                lily_msgbuf_add_text_range(msgbuf, str, start, i);

            lily_msgbuf_add_char(msgbuf, '\\');
            lily_msgbuf_add_char(msgbuf, escape_char);

            if (msgbuf->pos != 0) {
                /* This is intentional, because literals are user-defined and
                   could include %'s. This prevents an exploit. */
                lily_impl_debugf("%s", msgbuf->message);
                lily_msgbuf_reset(msgbuf);
            }

            start = i + 1;
        }
    }

    if (i != start)
        lily_msgbuf_add_text_range(msgbuf, str, start, i);

    lily_msgbuf_add(msgbuf, "\"\n");
    /* See above comment. */
    lily_impl_debugf("%s", msgbuf->message);
    lily_msgbuf_reset(msgbuf);
}

/* show_simple_value
   This handles printing str, integer, and number values that are not nil. This
   is used by show_code_sym for literals, and show_value for non-nil simple
   values. */
static void show_simple_value(lily_sig *sig, lily_value value,
        lily_msgbuf *msgbuf)
{
    int cls_id = sig->cls->id;

    if (cls_id == SYM_CLASS_STR)
        show_str(value.str->str, msgbuf);
    else if (cls_id == SYM_CLASS_INTEGER)
        lily_impl_debugf("%" PRId64 "\n", value.integer);
    else if (cls_id == SYM_CLASS_NUMBER)
        lily_impl_debugf("%f\n", value.number);
}

static void show_literal(lily_sig *sig, lily_value value, lily_msgbuf *msgbuf)
{
    lily_impl_debugf("(");
    show_sig(sig, NULL);
    lily_impl_debugf(") ");
    show_simple_value(sig, value, msgbuf);
}

static void show_register_info(lily_register_info *method_info, char *reg_type,
        int reg_num)
{
    lily_register_info reg_info = method_info[reg_num];
    lily_impl_debugf("(");
    show_sig(reg_info.sig, NULL);
    lily_impl_debugf(") %s register #%d", reg_type, reg_num);

    if (reg_info.name != NULL)
        lily_impl_debugf(" (%s from line %d)\n", reg_info.name,
                         reg_info.line_num);
    else
        lily_impl_debugf("\n");
}

static void write_indent(int indent)
{
    int i;
    for (i = 0;i < indent;i++)
        lily_impl_debugf("|    ");
}

/* show_code
   This shows all the code in a var that holds a method. This displays the
   operations in a simple way so that users can get an idea of what the vm is
   seeing.
   This function uses call info (ci) to determine how to handle given opcodes.
   This was done because many opcodes (all binary ones, for example), share the
   same basic structure. This also eliminates the need for specialized
   functions. */
static void show_code(lily_method_val *at_main, lily_method_val *mval,
        lily_msgbuf *msgbuf, int indent)
{
    char format[5];
    int digits, i, len;
    uintptr_t *code;

    digits = 0;
    i = 0;
    code = mval->code;
    len = mval->pos;

    while (len) {
        len /= 10;
        digits++;
    }

    len = mval->pos;
    format[0] = '%';
    if (digits >= 10) {
        format[1] = (digits / 10) + '0';
        format[2] = (digits % 10) + '0';
        format[3] = 'd';
        format[4] = '\0';
    }
    else {
        format[1] = digits + '0';
        format[2] = 'd';
        format[3] = '\0';
    }

    lily_register_info *method_info = mval->reg_info;
    int last_line_num = -1;
    /* Add one indent level for the line numbers under which code will be
       grouped. */
    indent++;

    while (i < len) {
        int opcode = code[i];
        const int *opcode_data = code_info_for_opcode(opcode);
        char *opcode_name = opcode_names[opcode];
        int count, data_code, is_global, j;

        /* Group under a new line number if the current one isn't the last one
           seen. This makes it easy to see what operations that are caused by
           a particular line number. After that, the [] indicates the position
           of i for extra debugging. */
        if (opcode_data[1] == D_LINENO && code[i+1] != last_line_num) {
            /* Line numbers are the heading, so don't indent those. */
            write_indent(indent - 1);
            lily_impl_debugf("|____ (line %d)\n", code[i+1]);
            last_line_num = code[i+1];
        }

        if (i != 0) {
            write_indent(indent);
            lily_impl_debugf("|\n");
        }

        if (indent)
            write_indent(indent);

        lily_impl_debugf("|____ [");
        lily_impl_debugf(format, i);
        lily_impl_debugf("] %s", opcode_name);
        /* A newline isn't printed after the opcode's name so that the line
           number can be on the same line. Most opcodes have a line number,
           except for a few where that does not apply.
           Most that do not have a special starting opcode specific to them that
           will write in the newline.
           o_jump doesn't, so write this in for it. */
        if (code[i] == o_jump)
            lily_impl_debugf("\n");

        for (j = 0;j < opcode_data[0];j++) {
            data_code = opcode_data[j+1];

            if (data_code == D_LINENO)
                lily_impl_debugf("\n");
            else if (data_code == D_INPUT) {
                if (indent)
                    write_indent(indent);
                lily_impl_debugf("|     <---- ");
                show_register_info(method_info, "local", code[i+j+1]);
            }
            else if (data_code == D_OUTPUT) {
                /* output is NULL if it's a method or function that does not
                   return a value. Omit this for brevity (the lack of a stated
                   output meaning it doesn't have one). */
                if (code[i+j+1] != -1) {
                    if (indent)
                        write_indent(indent);
                    lily_impl_debugf("|     ====> ");
                    show_register_info(method_info, "local", code[i+j+1]);
                }
            }
            else if (data_code == D_JUMP_ON) {
                if (code[i+1+j] == 0)
                    lily_impl_debugf(" false\n");
                else
                    lily_impl_debugf(" true\n");
            }
            else if (data_code == D_JUMP) {
                if (indent)
                    write_indent(indent);
                lily_impl_debugf("|     -> | [%d]\n", (int)code[i+j+1]);
            }
            else if (data_code == D_COUNT)
                count = (int)code[i+j+1];
            else if (data_code == D_COUNT_LIST) {
                if (count == 0)
                    i--;
                else {
                    int k;
                    for (k = 0;k < count;k++, i++) {
                        if (indent)
                            write_indent(indent);

                        lily_impl_debugf("|     <---- ");
                        show_register_info(method_info, "local", code[i+j+1]);
                    }
                    i--;
                }
            }
            else if (data_code == D_LIT_INPUT) {
                if (indent)
                    write_indent(indent);

                lily_impl_debugf("|     <---- ");
                lily_literal *lit = (lily_literal *)code[i+j+1];
                show_literal(lit->sig, lit->value, msgbuf);
            }
            else if (data_code == D_COUNT)
                count = (int)code[i+j+1];
            else if (data_code == D_INT_VAL) {
                if (indent)
                    write_indent(indent);

                lily_impl_debugf("|     <---- %d\n", (int)code[i+j+1]);
            }
            else if (data_code == D_NOP) {
                lily_impl_debugf("\n");
                break;
            }
            else if (data_code == D_GLOBAL_INPUT) {
                if (indent)
                    write_indent(indent);

                lily_impl_debugf("|     <---- ");
                /* Critical: This is a global, so use @main instead of the
                   current method. */
                show_register_info(at_main->reg_info, "global", code[i+j+1]);
            }
            else if (data_code == D_GLOBAL_OUTPUT) {
                /* This doesn't have to be checked because D_GLOBAL_OUTPUT is
                   only for writes to a global, and always exists. */
                if (indent)
                    write_indent(indent);

                lily_impl_debugf("|     ====> ");
                /* Critical: This is a global, so use @main instead of the
                   current method. */
                show_register_info(at_main->reg_info, "global", code[i+j+1]);
            }
            else if (data_code == D_IS_GLOBAL)
                is_global = code[i+j+1];
            else if (data_code == D_SHOW_INPUT) {
                if (indent)
                    write_indent(indent);

                lily_impl_debugf("|     <---- ");
                if (is_global)
                    show_register_info(at_main->reg_info, "global",
                            code[i+j+1]);
                else
                    show_register_info(method_info, "local", code[i+j+1]);
            }
        }
        i += j + 1;
    }
}

static void show_value(lily_method_val *, lily_sig *, lily_value, lily_msgbuf *,
        int);
static void show_list_value(lily_method_val *at_main, lily_sig *sig,
        lily_list_val *lv, lily_msgbuf *msgbuf, int indent)
{
    int i;
    lily_sig *elem_sig;

    /* This intentionally dives into circular refs so that (circular) can be
       written with proper indentation. */
    if (lv->visited) {
        if (indent > 1)
            write_indent(indent-1);
        lily_impl_debugf("(circular)\n");
        return;
    }

    lv->visited = 1;
    elem_sig = sig->siglist[0];
    for (i = 0;i < lv->num_values;i++) {
        /* Write out one blank line, so each value has a space between the next.
           This keeps things from appearing crammed together.
           Not needed for the first line though. */
        if (i != 0) {
            if (indent > 1)
                write_indent(indent-1);
            lily_impl_debugf("|\n");
        }
        if (indent > 1)
            write_indent(indent-1);

        lily_impl_debugf("|____");
        lily_impl_debugf("[%d] = ", i);

        if (lv->flags[i] & SYM_IS_NIL)
            lily_impl_debugf("(nil)\n");
        else
            show_value(at_main, elem_sig, lv->values[i], msgbuf, indent);
    }

    lv->visited = 0;
}

/* show_value
   This determines how to show a value given to 'show'. Most things are handled
   here, except for lists (which recursively call this for each non-nil value),
   and methods (which use show_code).
   Each command should end with an extra '\n' in some way. This consistency is
   important for making sure that any value sent to show results in the same
   amount of \n's written after it. */
static void show_value(lily_method_val *at_main, lily_sig *sig,
        lily_value value, lily_msgbuf *msgbuf, int indent)
{
    int cls_id = sig->cls->id;

    if (cls_id == SYM_CLASS_STR ||
        cls_id == SYM_CLASS_INTEGER ||
        cls_id == SYM_CLASS_NUMBER) {
        show_simple_value(sig, value, msgbuf);
        /* This always finishes with \n. */
    }
    else if (cls_id == SYM_CLASS_LIST) {
        lily_list_val *lv = value.list;

        show_sig(sig, NULL);
        lily_impl_debugf("\n");
        show_list_value(at_main, sig, lv, msgbuf, indent+1);
        /* The \n at the end comes from the last value's \n. */
    }
    else if (cls_id == SYM_CLASS_FUNCTION) {
        lily_function_val *fv = value.function;

        show_sig(sig, NULL);
        lily_impl_debugf(" %s\n", fv->trace_name);
    }
    else if (cls_id == SYM_CLASS_METHOD) {
        lily_method_val *mv = value.method;

        show_sig(sig, mv->trace_name);
        lily_impl_debugf("\n");
        show_code(at_main, mv, msgbuf, indent);
        /* The \n at the end comes from show_code always finishing that way. */
    }
    else if (cls_id == SYM_CLASS_OBJECT) {
        lily_object_val *ov = value.object;

        if (ov->sig == NULL)
            lily_impl_debugf("(nil)\n");
        else {
            lily_impl_debugf("(object) ");
            show_value(at_main, ov->sig, ov->value, msgbuf, indent);
        }
    }
}

/** API for lily_debug.c **/
/* lily_show_symtab
   This is the API function for debugging. Just send the symtab and debug will
   do the rest. */
void lily_show_symtab(lily_symtab *symtab, lily_msgbuf *msgbuf)
{
    lily_var *var = symtab->var_start;
    lily_var *at_main = var;

    /* Now, give information about all of the methods that have code assigned to
       them. Methods that are arguments get scoped out, and are thus ignored
       since they do not have code. */
    lily_impl_debugf("Showing all methods:\n");
    while (var != NULL) {
        if (var->sig->cls->id == SYM_CLASS_METHOD) {
            lily_impl_debugf("method %s @ line %d\n", var->name, var->line_num);
            show_code(at_main->value.method, var->value.method, msgbuf, 0);
        }
        var = var->next;
    }
}

/* lily_show_sym
   This handles showing the information for a symbol at vm-time. */
void lily_show_sym(lily_method_val *at_main, lily_vm_register *reg,
        int is_global, int reg_id, lily_msgbuf *msgbuf)
{
    char *scope_str;
    scope_str = is_global ? "global" : "local";

    lily_impl_debugf("Showing %s register #%d", scope_str, reg_id);
    if (reg->name != NULL)
        lily_impl_debugf(" (%s from line %d)\n", reg->name, reg->line_num);
    else
        lily_impl_debugf("\n");

    lily_impl_debugf("Type:  ");
    show_sig(reg->sig, NULL);
    lily_impl_debugf("\n");

    lily_impl_debugf("Value: ");
    if (reg->flags & SYM_IS_NIL)
        lily_impl_debugf("(nil)");
    else
        show_value(at_main, reg->sig, reg->value, msgbuf, 0);
}
