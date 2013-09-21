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

/* Opcodes that have line numbers also have extra space so they print the line
   number at an even spot. This saves debug from having to calculate how much
   (and possibly getting it wrong) at the cost of a little bit of memory.
   No extra space means it doesn't have a line number. */
char *opcode_names[33] = {
    "assign               ",
    "object assign        ",
    "assign (ref/deref)   ",
    "subscript assign     ",
    "integer add (+)      ",
    "integer minus (-)    ",
    "integer multiply (*) ",
    "integer divide (/)   ",
    "number add (+)       ",
    "number minus (-)     ",
    "number multiply (*)  ",
    "number divide (/)    ",
    "is equal (==)        ",
    "not equal (!=)       ",
    "less (<)             ",
    "less equal (<=)      ",
    "greater (>)          ",
    "greater equal (>=)   ",
    "jump",
    "jump if",
    "function call        ",
    "method call          ",
    "return value         ",
    "return (no value)    ",
    "save",
    "restore",
    "unary not (!x)       ",
    "unary minus (-x)     ",
    "build list           ",
    "subscript            ",
    "typecast             ",
    "show                 ",
    "return from vm       "
};

static const int call_ci[]       =
    {5, D_LINENO, D_INPUT, D_COUNT, D_COUNT_LIST, D_OUTPUT};
static const int build_list_ci[] =
    {4, D_LINENO, D_COUNT, D_COUNT_LIST, D_OUTPUT};
static const int sub_assign_ci[] = {4, D_LINENO, D_INPUT, D_INPUT, D_INPUT};
static const int binary_ci[]     = {4, D_LINENO, D_INPUT, D_INPUT, D_OUTPUT};
static const int in_out_ci[]     = {3, D_LINENO, D_INPUT, D_OUTPUT};
static const int jump_if_ci[]    = {3, D_JUMP_ON, D_INPUT, D_JUMP};
static const int save_ci[]       = {2, D_SHOW_COUNT, D_COUNT_LIST};
static const int return_ci[]     = {2, D_LINENO, D_OUTPUT};
static const int show_ci[]       = {2, D_LINENO, D_INPUT};
static const int return_nv_ci[]  = {1, D_LINENO};
static const int restore_ci[]    = {1, D_SHOW_COUNT};
static const int jump_ci[]       = {1, D_JUMP};
static const int nop_ci[]        = {1, D_NOP};

static void show_sig(lily_sig *sig, char *name)
{
    lily_impl_debugf(sig->cls->name);

    if (sig->cls->id == SYM_CLASS_METHOD ||
        sig->cls->id == SYM_CLASS_FUNCTION) {
        if (name != NULL)
            lily_impl_debugf(" %s", name);

        lily_call_sig *csig = sig->node.call;
        lily_impl_debugf(" (");
        int i;
        for (i = 0;i < csig->num_args-1;i++) {
            show_sig(csig->args[i], NULL);
            lily_impl_debugf(", ");
        }
        if (i != csig->num_args) {
            show_sig(csig->args[i], NULL);
            if (csig->is_varargs)
                lily_impl_debugf("...");
        }
        lily_impl_debugf("):");
        if (csig->ret == NULL)
            lily_impl_debugf("nil");
        else
            show_sig(csig->ret, NULL);
    }
    else if (sig->cls->id == SYM_CLASS_LIST) {
        lily_impl_debugf("[");
        show_sig(sig->node.value_sig, NULL);
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
        case o_return_from_vm:
            ret = nop_ci;
            break;
        case o_return_noval:
            ret = return_nv_ci;
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
        case o_show:
            ret = show_ci;
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

static void show_code_sym(lily_sym *sym, lily_msgbuf *msgbuf)
{
    lily_impl_debugf("(");
    show_sig(sym->sig, NULL);
    lily_impl_debugf(") ");

    if (sym->flags & VAR_SYM) {
        lily_var *var = (lily_var *)sym;
        lily_impl_debugf("var %s (from line %d).\n", var->name,
                var->line_num);
    }
    else if (sym->flags & LITERAL_SYM)
        show_simple_value(sym->sig, sym->value, msgbuf);
    else if (sym->flags & STORAGE_SYM)
        lily_impl_debugf("storage at %p.\n", sym);
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
static void show_code(lily_method_val *mval, lily_msgbuf *msgbuf, int indent)
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

    while (i < len) {
        int opcode = code[i];
        const int *opcode_data = code_info_for_opcode(opcode);
        char *opcode_name = opcode_names[opcode];
        int count, data_code, j;

        if (i != 0) {
            write_indent(indent);
            lily_impl_debugf("|\n");
        }

        if (indent != 0)
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
                lily_impl_debugf("   (at line %d)\n", (int)code[i+j+1]);
            else if (data_code == D_INPUT) {
                if (indent != 0)
                    write_indent(indent);
                lily_impl_debugf("|     <---- ");
                show_code_sym((lily_sym *)code[i+j+1], msgbuf);
            }
            else if (data_code == D_OUTPUT) {
                /* output is NULL if it's a method or function that does not
                   return a value. Omit this for brevity (the lack of a stated
                   output meaning it doesn't have one). */
                if ((lily_sym *)code[i+j+1] != NULL) {
                    if (indent != 0)
                        write_indent(indent);
                    lily_impl_debugf("|     ====> ");
                    show_code_sym((lily_sym *)code[i+j+1], msgbuf);
                }
            }
            else if (data_code == D_JUMP_ON) {
                if (code[i+1+j] == 0)
                    lily_impl_debugf(" false\n");
                else
                    lily_impl_debugf(" true\n");
            }
            else if (data_code == D_JUMP) {
                if (indent != 0)
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
                        if (indent != 0)
                            write_indent(indent);

                        lily_impl_debugf("|     <---- ");
                        show_code_sym((lily_sym *)code[i+j+1], msgbuf);
                    }
                    i--;
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

static void show_value(lily_sig *, lily_value, lily_msgbuf *, int);

static void show_list_value(lily_sig *sig, lily_list_val *lv,
        lily_msgbuf *msgbuf, int indent)
{
    int i, j;
    lily_sig *elem_sig;

    /* This intentionally dives into circular refs so that (circular) can be
       written with proper indentation. */
    if (lv->visited) {
        if (indent > 1)
            write_indent(indent-1);
        lily_impl_debugf("(circular)");
        return;
    }

    lv->visited = 1;
    elem_sig = sig->node.value_sig;
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

        if (lv->flags[i] & S_IS_NIL)
            lily_impl_debugf("(nil)");
        else
            show_value(elem_sig, lv->values[i], msgbuf, indent);
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
static void show_value(lily_sig *sig, lily_value value, lily_msgbuf *msgbuf,
        int indent)
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
        show_list_value(sig, lv, msgbuf, indent+1);
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
        show_code(mv, msgbuf, indent);
        /* The \n at the end comes from show_code always finishing that way. */
    }
    else if (cls_id == SYM_CLASS_OBJECT) {
        lily_object_val *ov = value.object;

        if (ov->sig == NULL)
            lily_impl_debugf("(nil)");
        else {
            lily_impl_debugf("(object) ");
            show_value(ov->sig, ov->value, msgbuf, indent);
        }
    }
}

/** API for lily_debug.c **/

/* lily_show_sym
   This is the workhorse for the show keyword. This shows data on any kind of
   symbol (even literals and storages). A msgbuf is also used in case there are
   literal strings to print. */
void lily_show_sym(lily_sym *sym, lily_msgbuf *msgbuf)
{
    lily_impl_debugf("Symbol: ");
    if (sym->flags & VAR_SYM) {
        lily_var *var = (lily_var *)sym;

        lily_impl_debugf("var ");
        show_sig(var->sig, var->name);
        lily_impl_debugf(" @ line %d\n", var->line_num);
    }
    else if (sym->flags & LITERAL_SYM) {
        show_sig(sym->sig, NULL);
        lily_impl_debugf(" literal\n");
    }
    else if (sym->flags & STORAGE_SYM) {
        show_sig(sym->sig, NULL);
        lily_impl_debugf(" storage at %p\n", sym);
    }
    else {
        show_sig(sym->sig, NULL);
        lily_impl_debugf(" mystery sym at %p\n", sym);
    }

    int cls_id = sym->sig->cls->id;
    if (cls_id != SYM_CLASS_INTEGER && cls_id != SYM_CLASS_NUMBER) {
        lily_generic_val *gv = sym->value.generic;
        lily_impl_debugf("Refcount: ");
        if (sym->flags & S_IS_NIL)
            lily_impl_debugf("0\n");
        else
            lily_impl_debugf("%d\n", gv->refcount);
    }

    lily_impl_debugf("Value: ");
    if (sym->flags & S_IS_NIL)
        lily_impl_debugf("(nil)");
    else
        show_value(sym->sig, sym->value, msgbuf, 0);
}

/* lily_show_symtab
   This is the API function for debugging. Just send the symtab and debug will
   do the rest. */
void lily_show_symtab(lily_symtab *symtab, lily_msgbuf *msgbuf)
{
    lily_var *var = symtab->var_start;

    /* Now, give information about all of the methods that have code assigned to
       them. Methods that are arguments get scoped out, and are thus ignored
       since they do not have code. */
    lily_impl_debugf("Showing all methods:\n");
    while (var != NULL) {
        if (var->sig->cls->id == SYM_CLASS_METHOD) {
            lily_impl_debugf("method %s @ line %d\n", var->name, var->line_num);
            show_code(var->value.method, msgbuf, 0);
        }
        var = var->next;
    }
}
