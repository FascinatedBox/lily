#include <inttypes.h>
#include <string.h>

#include "lily_impl.h"
#include "lily_msgbuf.h"
#include "lily_syminfo.h"
#include "lily_opcode.h"

/** Debug is responsible for:
    * Pretty printing out the code for all methods that have been scanned,
      notably __main__.

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
#define D_LINENO           0
/* D_INPUT:           This specifies a symbol that is being read. */
#define D_INPUT            1
/* D_OUTPUT:          The opcode's result will be written to this place. */
#define D_OUTPUT           2
/* D_NOP:             This position does not do anything. */
#define D_NOP              3
/* D_JUMP:            This position contains a jump to a future location. The
                      position is an int, not a sym. */
#define D_JUMP             4
/* D_JUMP_ON:         This contains 1 or 0. This is used to determine if
                      o_jump_if should jump on truth or false value. */
#define D_JUMP_ON          5
/* D_COUNT:           This specifies a number of arguments or values to come.
                      This value is stored but not shown. */
#define D_COUNT            8
/* D_COUNT_LIST:      This specifies the start of an argument list, using the
                      value recorded by D_COUNT. */
#define D_COUNT_LIST       9
/* D_INT_VAL:         Show a value that's just an integer. This is used by
                      o_for_setup to determine if it should init the step value
                      or not. */
#define D_INT_VAL         10
/* D_LIT_INPUT:       The input is the address of a literal. The literal's
                      value is shown. */
#define D_LIT_INPUT       11
/* D_GLOBAL_INPUT:    The INput is the address of a global register. */
#define D_GLOBAL_INPUT    12
/* D_GLOBAL_OUTPUT:   The OUTput is the address of a global register. */
#define D_GLOBAL_OUTPUT   13
/* D_IS_GLOBAL:       This specifies if an upcoming value is a global or a
                      local. This is used by show, where the value might be a
                      global var, or a local one. */
#define D_IS_GLOBAL       14
/* D_SHOW_INPUT:      A value given to show. This can be either a global or a
                      local register. */
#define D_SHOW_INPUT      15
/* D_CALL_INPUT_TYPE: This is used by calls to determine how the call is
                      stored:
                      0: The input is a readonly var.
                      1: The input is a local register. */
#define D_CALL_INPUT_TYPE 16
/* D_CALL_INPUT:      Input to either a method or function call. This is shown
                      according to what D_CALL_INPUT_TYPE picked up. */
#define D_CALL_INPUT      17

/** Flags for show_register_info: **/
/* This means the number given is for a register in __main__. By default, the
   current method's info is used. */
#define RI_GLOBAL 0x1
/* This is an input value, and should have an input prefix. */
#define RI_INPUT  0x2
/* This is an output value, and should have an output prefix. These two are not
   to be used together. However, both can be omitted if the value isn't an input
   or output to a particular place. */
#define RI_OUTPUT 0x4

typedef struct lily_debug_state_t {
    lily_method_val *main_method;
    lily_method_val *current_method;
    lily_msgbuf *msgbuf;
    int indent;
    void *data;
} lily_debug_state;

/* Opcodes that have line numbers also have extra space so they print the line
   number at an even spot. This saves debug from having to calculate how much
   (and possibly getting it wrong) at the cost of a little bit of memory.
   No extra space means it doesn't have a line number. */
char *opcode_names[47] = {
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
    "build hash",
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
    "package set",
    "package get",
    "return from vm"
};

static const int for_setup_ci[] =
    {6, D_LINENO, D_INPUT, D_INPUT, D_INPUT, D_INPUT, D_INT_VAL};
static const int for_integer_ci[] =
    {6, D_LINENO, D_INPUT, D_INPUT, D_INPUT, D_INPUT, D_JUMP};
static const int call_ci[]        =
    {6, D_LINENO, D_CALL_INPUT_TYPE, D_CALL_INPUT, D_COUNT, D_COUNT_LIST,
        D_OUTPUT};
static const int package_get_ci[] =
    {5, D_LINENO, D_GLOBAL_INPUT, D_INT_VAL, D_OUTPUT};
static const int package_set_ci[] =
    {5, D_LINENO, D_GLOBAL_INPUT, D_INT_VAL, D_INPUT};
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

static void write_msgbuf(lily_debug_state *debug)
{
    lily_impl_puts(debug->data, debug->msgbuf->message);
    lily_msgbuf_flush(debug->msgbuf);
}

static const int *code_info_for_opcode(lily_debug_state *debug, int opcode)
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
        case o_build_hash:
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
        case o_package_set:
            ret = package_set_ci;
            break;
        case o_package_get:
            ret = package_get_ci;
            break;
        default:
            lily_msgbuf_add_fmt(debug->msgbuf,
                    "warning: Opcode %d has no ci.\n",opcode);
            write_msgbuf(debug);
            ret = NULL;
            break;
    }

    return ret;
}

/* show_simple_value
   This handles printing str, integer, and number values that are not nil. This
   is used by show_code_sym for literals, and show_value for non-nil simple
   values. */
static void show_simple_value(lily_debug_state *debug, lily_sig *sig,
        lily_raw_value value)
{
    int cls_id = sig->cls->id;

    if (cls_id == SYM_CLASS_STR)
        lily_msgbuf_add_fmt(debug->msgbuf, "\"^E\"", value.str->str);
    else if (cls_id == SYM_CLASS_INTEGER)
        lily_msgbuf_add_int(debug->msgbuf, value.integer);
    else if (cls_id == SYM_CLASS_NUMBER)
        lily_msgbuf_add_double(debug->msgbuf, value.number);

    write_msgbuf(debug);
}

static void show_literal(lily_debug_state *debug, lily_sig *sig,
        lily_raw_value value)
{
    lily_msgbuf_add_fmt(debug->msgbuf, "(^T) ", sig);
    write_msgbuf(debug);
    show_simple_value(debug, sig, value);
    lily_impl_puts(debug->data, "\n");
}

static void show_readonly_var(lily_debug_state *debug, lily_var *var)
{
    lily_msgbuf_add_fmt(debug->msgbuf, "^I|     <---- (^T) ", debug->indent,
            var->sig);

    if (var->line_num != 0)
        lily_msgbuf_add_fmt(debug->msgbuf, "%s from line %d\n", var->name,
                var->line_num);
    else
        lily_msgbuf_add_fmt(debug->msgbuf, "%s [builtin]\n", var->name);

    write_msgbuf(debug);
}

static void show_register_info(lily_debug_state *debug, int flags, int reg_num)
{
    lily_register_info reg_info;
    char *arrow_str, *scope_str;
    lily_msgbuf *msgbuf = debug->msgbuf;

    if (flags & RI_GLOBAL) {
        scope_str = "global";
        reg_info = debug->main_method->reg_info[reg_num];
    }
    else {
        scope_str = "local";
        reg_info = debug->current_method->reg_info[reg_num];
    }

    if (flags & RI_INPUT)
        arrow_str = "|     <---- ";
    else if (flags & RI_OUTPUT)
        arrow_str = "|     ====> ";
    else
        arrow_str = "";

    lily_msgbuf_add_fmt(debug->msgbuf, "^I%s(^T) %s register #%d",
            debug->indent, arrow_str, reg_info.sig, scope_str, reg_num);

    if (reg_info.name != NULL) {
        lily_msgbuf_add(msgbuf, " (");
        if (reg_info.class_name)
            lily_msgbuf_add_fmt(msgbuf, "%s%s", reg_info.class_name, "::");

        if (reg_info.line_num != 0)
            lily_msgbuf_add_fmt(msgbuf, "%s from line %d)\n", reg_info.name,
                             reg_info.line_num);
        else
            lily_msgbuf_add_fmt(msgbuf, "%s [builtin])\n", reg_info.name);
    }
    else
        lily_msgbuf_add(msgbuf, "\n");

    write_msgbuf(debug);
}

/* show_code
   This shows all the code in a var that holds a method. This displays the
   operations in a simple way so that users can get an idea of what the vm is
   seeing.
   This function uses call info (ci) to determine how to handle given opcodes.
   This was done because many opcodes (all binary ones, for example), share the
   same basic structure. This also eliminates the need for specialized
   functions. */
static void show_code(lily_debug_state *debug)
{
    char format[5];
    int digits, i, len;
    uintptr_t *code;
    lily_msgbuf *msgbuf = debug->msgbuf;
    void *data = debug->data;

    digits = 0;
    i = 0;
    code = debug->current_method->code;
    len = debug->current_method->pos;

    while (len) {
        len /= 10;
        digits++;
    }

    len = debug->current_method->pos;
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

    int last_line_num = -1;
    int indent;

    /* Add one indent level for the line numbers under which code will be
       grouped. */
    debug->indent++;
    indent = debug->indent;

    while (i < len) {
        int opcode = code[i];
        const int *opcode_data = code_info_for_opcode(debug, opcode);
        char *opcode_name = opcode_names[opcode];
        int call_input_type, count, data_code, is_global, j;

        /* Group under a new line number if the current one isn't the last one
           seen. This makes it easy to see what operations that are caused by
           a particular line number. After that, the [] indicates the position
           of i for extra debugging. */
        if (opcode_data[1] == D_LINENO && code[i+1] != last_line_num) {
            /* Line numbers are the heading, so don't indent those. */
            lily_msgbuf_add_fmt(msgbuf, "^I|____ (line %d)\n", indent - 1,
                    code[i+1]);
            last_line_num = code[i+1];
        }
        if (i != 0)
            lily_msgbuf_add_fmt(msgbuf, "^I|\n", indent);

        lily_msgbuf_add_fmt(msgbuf, "^I|____ [", indent);
        lily_msgbuf_add_fmt(msgbuf, format, i);
        lily_msgbuf_add_fmt(msgbuf, "] %s", opcode_name);
        write_msgbuf(debug);
        /* A newline isn't printed after the opcode's name so that the line
           number can be on the same line. Most opcodes have a line number,
           except for a few where that does not apply.
           Most that do not have a special starting opcode specific to them that
           will write in the newline.
           o_jump doesn't, so write this in for it. */
        if (code[i] == o_jump)
            lily_impl_puts(data, "\n");

        for (j = 1;j <= opcode_data[0];j++) {
            data_code = opcode_data[j];

            if (data_code == D_LINENO)
                lily_impl_puts(data, "\n");
            else if (data_code == D_INPUT)
                show_register_info(debug, RI_INPUT, code[i+j]);
            else if (data_code == D_OUTPUT) {
                /* output is NULL if it's a method or function that does not
                   return a value. Omit this for brevity (the lack of a stated
                   output meaning it doesn't have one). */
                if (code[i+j] != -1)
                    show_register_info(debug, RI_OUTPUT, code[i+j]);
            }
            else if (data_code == D_JUMP_ON) {
                if (code[i+j] == 0)
                    lily_impl_puts(data, " false\n");
                else
                    lily_impl_puts(data, " true\n");
            }
            else if (data_code == D_JUMP) {
                lily_msgbuf_add_fmt(msgbuf, "^I|     -> | [%d]\n",
                        (int)code[i+j]);
                write_msgbuf(debug);
            }
            else if (data_code == D_COUNT)
                count = (int)code[i+j];
            else if (data_code == D_COUNT_LIST) {
                if (count == 0)
                    i--;
                else {
                    int k;
                    for (k = 0;k < count;k++, i++)
                        show_register_info(debug, RI_INPUT, code[i+j]);

                    i--;
                }
            }
            else if (data_code == D_LIT_INPUT) {
                lily_msgbuf_add_fmt(msgbuf, "^I|     <---- ", indent);
                write_msgbuf(debug);

                lily_literal *lit = (lily_literal *)code[i+j];
                show_literal(debug, lit->sig, lit->value);
            }
            else if (data_code == D_COUNT)
                count = (int)code[i+j];
            else if (data_code == D_INT_VAL) {
                lily_msgbuf_add_fmt(msgbuf, "^I|     <---- %d\n",
                        indent, (int)code[i+j]);
                write_msgbuf(debug);
            }
            else if (data_code == D_NOP) {
                lily_impl_puts(data, "\n");
                break;
            }
            else if (data_code == D_GLOBAL_INPUT)
                show_register_info(debug, RI_GLOBAL | RI_INPUT, code[i+j]);
            else if (data_code == D_GLOBAL_OUTPUT) {
                /* This doesn't have to be checked because D_GLOBAL_OUTPUT is
                   only for writes to a global, and always exists. */
                show_register_info(debug, RI_GLOBAL | RI_OUTPUT, code[i+j]);
            }
            else if (data_code == D_IS_GLOBAL)
                is_global = code[i+j];
            else if (data_code == D_SHOW_INPUT) {
                int flags = RI_INPUT;
                if (is_global)
                    flags |= RI_GLOBAL;

                show_register_info(debug, flags, code[i+j]);
            }
            else if (data_code == D_CALL_INPUT_TYPE)
                call_input_type = code[i+j];
            else if (data_code == D_CALL_INPUT) {
                if (call_input_type == 1)
                    show_readonly_var(debug, (lily_var *)code[i+j]);
                else
                    show_register_info(debug, RI_INPUT, code[i+j]);
            }
        }
        i += j;
    }
}

static void show_value(lily_debug_state *debug, lily_value *);

static void show_list_value(lily_debug_state *debug, lily_sig *sig,
        lily_list_val *lv)
{
    int i, indent;
    lily_msgbuf *msgbuf = debug->msgbuf;

    indent = debug->indent;

    /* This intentionally dives into circular refs so that (circular) can be
       written with proper indentation. */
    if (lv->visited) {
        lily_msgbuf_add_fmt(msgbuf, "^I(circular)\n");
        write_msgbuf(debug);
        return;
    }

    lv->visited = 1;
    for (i = 0;i < lv->num_values;i++) {
        /* Write out one blank line, so each value has a space between the next.
           This keeps things from appearing crammed together.
           Not needed for the first line though. */
        if (i != 0)
            lily_msgbuf_add_fmt(msgbuf, "^I|\n", indent - 1);

        lily_msgbuf_add_fmt(msgbuf, "^I|____[%d] = ", indent - 1, i);
        write_msgbuf(debug);

        show_value(debug, lv->elems[i]);
    }

    lv->visited = 0;
}

static void show_hash_value(lily_debug_state *debug, lily_sig *sig,
        lily_hash_val *hash_val)
{
    int indent;
    lily_sig *key_sig;
    lily_hash_elem *elem_iter;
    lily_msgbuf *msgbuf = debug->msgbuf;

    indent = debug->indent;

    /* This intentionally dives into circular refs so that (circular) can be
       written with proper indentation. */
    if (hash_val->visited) {
        lily_msgbuf_add_fmt(msgbuf, "^I(circular)\n", indent - 1);
        write_msgbuf(debug);
        return;
    }

    hash_val->visited = 1;
    key_sig = sig->siglist[0];
    elem_iter = hash_val->elem_chain;
    while (elem_iter) {
        /* Write out one blank line, so each value has a space between the next.
           This keeps things from appearing crammed together.
           Not needed for the first line though. */
        if (elem_iter != hash_val->elem_chain)
            lily_msgbuf_add_fmt(msgbuf, "^I|\n", indent - 1);

        lily_msgbuf_add_fmt(msgbuf, "^I|____[", indent - 1);
        write_msgbuf(debug);
        /* vm does not allow creating hashes with nil keys, so this should be
           safe. */
        show_simple_value(debug, key_sig, elem_iter->elem_key->value);
        lily_impl_puts(debug->data, "] = ");

        show_value(debug, elem_iter->elem_value);

        elem_iter = elem_iter->next;
    }

    hash_val->visited = 0;
}

/* show_value
   This determines how to show a value given to 'show'. Most things are handled
   here, except for lists (which recursively call this for each non-nil value),
   and methods (which use show_code).
   Each command should end with an extra '\n' in some way. This consistency is
   important for making sure that any value sent to show results in the same
   amount of \n's written after it. */
static void show_value(lily_debug_state *debug, lily_value *value)
{
    lily_sig *sig = value->sig;
    int cls_id = sig->cls->id;
    lily_raw_value raw_value = value->value;

    if (value->flags & VAL_IS_NIL) {
        lily_impl_puts(debug->data, "(nil)\n");
        return;
    }

    if (cls_id == SYM_CLASS_STR ||
        cls_id == SYM_CLASS_INTEGER ||
        cls_id == SYM_CLASS_NUMBER) {
        show_simple_value(debug, sig, raw_value);
        lily_impl_puts(debug->data, "\n");
    }
    else if (cls_id == SYM_CLASS_LIST ||
             cls_id == SYM_CLASS_HASH) {
        lily_list_val *lv = NULL;
        lily_hash_val *hv = NULL;

        if (cls_id == SYM_CLASS_LIST)
            lv = raw_value.list;
        else
            hv = raw_value.hash;

        lily_msgbuf_add_fmt(debug->msgbuf, "^T\n", sig);
        write_msgbuf(debug);

        debug->indent++;
        if (cls_id == SYM_CLASS_LIST)
            show_list_value(debug, sig, lv);
        else
            show_hash_value(debug, sig, hv);
        debug->indent--;
        /* The \n at the end comes from the last value's \n. */
    }
    else if (cls_id == SYM_CLASS_FUNCTION) {
        lily_function_val *fv = raw_value.function;

        lily_msgbuf_add_fmt(debug->msgbuf, "^T %s\n", sig, fv->trace_name);
        write_msgbuf(debug);
    }
    else if (cls_id == SYM_CLASS_METHOD) {
        lily_method_val *mv = raw_value.method;
        lily_method_val *save_current;

        lily_msgbuf_add_fmt(debug->msgbuf, "^T %s\n", sig, mv->trace_name);
        write_msgbuf(debug);

        save_current = debug->current_method;
        debug->current_method = mv;
        show_code(debug);
        debug->current_method = save_current;
        /* The \n at the end comes from show_code always finishing that way. */
    }
    else if (cls_id == SYM_CLASS_OBJECT) {
        lily_value *obj_value = raw_value.object->inner_value;

        /* Don't condense, or it'll be (object) (nil), which seems a bit
           strange. */
        if (obj_value->flags & VAL_IS_NIL)
            lily_impl_puts(debug->data, "(nil)\n");
        else {
            lily_impl_puts(debug->data, "(object) ");
            show_value(debug, obj_value);
        }
    }
    /* A show for package is not included because it's not currently possible
       to see packages. Any time that a package value is seen, the parser
       enforces a check for ::, so only elements of a package get seen. */
}

/** API for lily_debug.c **/
/* lily_show_sym
   This handles showing the information for a symbol at vm-time. */
void lily_show_sym(lily_method_val *lily_main, lily_method_val *current_method,
        lily_value *value, int is_global, int reg_id, lily_msgbuf *msgbuf,
        void *data)
{
    lily_debug_state debug;
    debug.indent = 0;
    debug.main_method = lily_main;
    debug.current_method = current_method;
    debug.msgbuf = msgbuf;
    debug.data = data;

    int flags = 0;
    if (is_global)
        flags |= RI_GLOBAL;

    lily_impl_puts(data, "Showing ");
    show_register_info(&debug, flags, reg_id);

    lily_impl_puts(data, "Value: ");
    show_value(&debug, value);
}
