#include <inttypes.h>
#include <string.h>

#include "lily_impl.h"
#include "lily_msgbuf.h"
#include "lily_syminfo.h"
#include "lily_opcode.h"
#include "lily_vm.h"

/** Debug is responsible for pretty printing whatever value it's given. This
    may entail functions, lists, hashes, and more. For native functions, the
    code inside of the function gets dumped. This is used to determine if an
    error is within the emitter's code generation or the vm. **/

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
/* D_LIT_INPUT:       The input is a position in the vm's table of literals. */
#define D_LIT_INPUT       11
/* D_GLOBAL_INPUT:    The INput is the address of a global register. */
#define D_GLOBAL_INPUT    12
/* D_GLOBAL_OUTPUT:   The OUTput is the address of a global register. */
#define D_GLOBAL_OUTPUT   13
/* D_IS_GLOBAL:       This specifies if an upcoming value is a global or a
                      local. This is used by show, where the value might be a
                      global var, or a local one. */
#define D_IS_GLOBAL       14
/* D_COND_INPUT:      This follows D_IS_GLOBAL. If D_IS_GLOBAL's position was
                      1, this register is a global. Otherwise, the register is
                      a local. */
#define D_COND_INPUT      15
/* D_CALL_TYPE:       This is used by calls to determine how the call is stored:
                      0: The input is a readonly var.
                      1: The input is a local register. */
#define D_CALL_TYPE       16
/* D_CALL_INPUT:      Input to a function call. This is shown according to what
                      D_CALL_INPUT_TYPE picked up. */
#define D_CALL_INPUT      17
/* D_FUNC_INPUT:      This is a position in the vm's table of functions. */
#define D_FUNC_INPUT      18

/** Flags for show_register_info: **/
/* This means the number given is for a register in __main__. By default, the
   current function's info is used. */
#define RI_GLOBAL 0x1
/* This is an input value, and should have an input prefix. */
#define RI_INPUT  0x2
/* This is an output value, and should have an output prefix. These two are not
   to be used together. However, both can be omitted if the value isn't an input
   or output to a particular place. */
#define RI_OUTPUT 0x4

typedef struct lily_debug_state_t {
    lily_function_val *main_function;
    lily_function_val *current_function;
    lily_msgbuf *msgbuf;
    int indent;
    lily_vm_state *vm;
    void *data;
} lily_debug_state;

/* Opcodes that have line numbers also have extra space so they print the line
   number at an even spot. This saves debug from having to calculate how much
   (and possibly getting it wrong) at the cost of a little bit of memory.
   No extra space means it doesn't have a line number. */
char *opcode_names[53] = {
    "assign",
    "any assign",
    "assign (ref/deref)",
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
    "double add (+)",
    "double minus (-)",
    "double multiply (*)",
    "double divide (/)",
    "is equal (==)",
    "not equal (!=)",
    "less (<)",
    "less equal (<=)",
    "greater (>)",
    "greater equal (>=)",
    "jump",
    "jump if",
    "function call",
    "return value",
    "return (no value)",
    "unary not (!x)",
    "unary minus (-x)",
    "build list/tuple",
    "build hash",
    "typecast",
    "integer <-> double",
    "return expected",
    "for (integer range)",
    "for setup",
    "get item",
    "set item",
    "get global",
    "set global",
    "get const",
    "get function",
    "package set",
    "package get",
    "push try",
    "pop try",
    "except",
    "raise",
    "isnil",
    "new instance",
    "return from vm"
};

static const int optable[][8] = {
    {o_assign,              3, D_LINENO,  D_INPUT,        D_OUTPUT,        -1,            -1,           -1},
    {o_any_assign,          3, D_LINENO,  D_INPUT,        D_OUTPUT,        -1,            -1,           -1},
    {o_ref_assign,          3, D_LINENO,  D_INPUT,        D_OUTPUT,        -1,            -1,           -1},
    {o_integer_add,         4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_integer_minus,       4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_modulo,              4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_integer_mul,         4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_integer_div,         4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_left_shift,          4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_right_shift,         4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_bitwise_and,         4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_bitwise_or,          4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_bitwise_xor,         4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_double_add,          4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_double_minus,        4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_double_mul,          4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_double_div,          4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_is_equal,            4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_not_eq,              4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_less,                4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_less_eq,             4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_greater,             4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_greater_eq,          4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_jump,                1, D_JUMP,    -1,             -1,              -1,            -1,           -1},
    {o_jump_if,             3, D_JUMP_ON, D_INPUT,        D_JUMP,          -1,            -1,           -1},
    {o_function_call,       6, D_LINENO,  D_CALL_TYPE,    D_CALL_INPUT,    D_COUNT,       D_COUNT_LIST, D_OUTPUT},
    {o_return_val,          2, D_LINENO,  D_INPUT,        -1,              -1,            -1,           -1},
    {o_return_noval,        1, D_LINENO,  -1,             -1,              -1,            -1,           -1},
    {o_unary_not,           3, D_LINENO,  D_INPUT,        D_OUTPUT,        -1,            -1,           -1},
    {o_unary_minus,         3, D_LINENO,  D_INPUT,        D_OUTPUT,        -1,            -1,           -1},
    {o_build_list_tuple,    4, D_LINENO,  D_COUNT,        D_COUNT_LIST,    D_OUTPUT,      -1,           -1},
    {o_build_hash,          4, D_LINENO,  D_COUNT,        D_COUNT_LIST,    D_OUTPUT,      -1,           -1},
    {o_any_typecast,        3, D_LINENO,  D_INPUT,        D_OUTPUT,        -1,            -1,           -1},
    {o_intdbl_typecast,     3, D_LINENO,  D_INPUT,        D_OUTPUT,        -1,            -1,           -1},
    {o_return_expected,     1, D_LINENO,  -1,             -1,              -1,            -1,           -1},
    {o_integer_for,         6, D_LINENO,  D_INPUT,        D_INPUT,         D_INPUT,       D_INPUT,      D_JUMP},
    {o_for_setup,           6, D_LINENO,  D_INPUT,        D_INPUT,         D_INPUT,       D_INPUT,      D_INT_VAL},
    {o_get_item,            4, D_LINENO,  D_INPUT,        D_INPUT,         D_OUTPUT,      -1,           -1},
    {o_set_item,            4, D_LINENO,  D_INPUT,        D_INPUT,         D_INPUT,       -1,           -1},
    {o_get_global,          3, D_LINENO,  D_GLOBAL_INPUT, D_OUTPUT         -1,            -1,           -1},
    {o_set_global,          3, D_LINENO,  D_INPUT,        D_GLOBAL_OUTPUT, -1,            -1,           -1},
    {o_get_const,           3, D_LINENO,  D_LIT_INPUT,    D_OUTPUT,        -1,            -1,           -1},
    {o_get_function,        3, D_LINENO,  D_FUNC_INPUT,   D_OUTPUT,        -1,            -1,           -1},
    {o_package_set,         4, D_LINENO,  D_GLOBAL_INPUT, D_INT_VAL,       D_INPUT,       -1,           -1},
    {o_package_get,         4, D_LINENO,  D_GLOBAL_INPUT, D_INT_VAL,       D_OUTPUT,      -1            -1},
    {o_push_try,            2, D_LINENO,  D_JUMP          -1,              -1,            -1,           -1},
    {o_pop_try,             1, D_NOP,     -1,             -1,              -1,            -1,           -1},
    {o_except,              4, D_LINENO,  D_JUMP,         D_INT_VAL,       D_OUTPUT,      -1,           -1},
    {o_raise,               2, D_LINENO,  D_INPUT         -1,              -1,            -1,           -1},
    {o_isnil,               4, D_LINENO,  D_IS_GLOBAL,    D_COND_INPUT,    D_OUTPUT,      -1,           -1},
    {o_new_instance,        2, D_LINENO,  D_OUTPUT,       -1,              -1,            -1,           -1},
    {o_return_from_vm,      1, D_NOP,     -1,             -1,              -1,            -1,           -1}
};

static void write_msgbuf(lily_debug_state *debug)
{
    lily_impl_puts(debug->data, debug->msgbuf->message);
    lily_msgbuf_flush(debug->msgbuf);
}

/*  show_simple_value
    Show an integer, double, or string literal. */
static void show_simple_value(lily_debug_state *debug, lily_sig *sig,
        lily_raw_value value)
{
    int cls_id = sig->cls->id;

    if (cls_id == SYM_CLASS_STRING)
        lily_msgbuf_add_fmt(debug->msgbuf, "\"^E\"", value.string->string);
    else if (cls_id == SYM_CLASS_INTEGER)
        lily_msgbuf_add_int(debug->msgbuf, value.integer);
    else if (cls_id == SYM_CLASS_DOUBLE)
        lily_msgbuf_add_double(debug->msgbuf, value.doubleval);

    write_msgbuf(debug);
}

/*  show_literal
    Show a literal value for show_code. */
static void show_literal(lily_debug_state *debug, int lit_pos)
{
    lily_literal *lit = debug->vm->literal_table[lit_pos];
    lily_msgbuf_add_fmt(debug->msgbuf, "(^T) ", lit->sig);
    write_msgbuf(debug);
    show_simple_value(debug, lit->sig, lit->value);
    lily_impl_puts(debug->data, "\n");
}

/*  show_function
    Show the name of a function at the given position. */
static void show_function(lily_debug_state *debug, int position)
{
    lily_var *var = debug->vm->function_table[position];
    lily_msgbuf_add_fmt(debug->msgbuf, "^I|     <---- (^T) ", debug->indent,
            var->sig);

    if (var->parent != NULL)
        lily_msgbuf_add_fmt(debug->msgbuf, "%s::", var->parent->name);

    if (var->line_num != 0)
        lily_msgbuf_add_fmt(debug->msgbuf, "%s from line %d\n", var->name,
                var->line_num);
    else
        lily_msgbuf_add_fmt(debug->msgbuf, "%s [builtin]\n", var->name);

    write_msgbuf(debug);
}

/*  show_register_info
    Show information about a given register which may or may not be a global.
    This is called by show_code to print out what registers are being used. */
static void show_register_info(lily_debug_state *debug, int flags, int reg_num)
{
    lily_register_info reg_info;
    char *arrow_str, *scope_str;
    lily_msgbuf *msgbuf = debug->msgbuf;

    if (flags & RI_GLOBAL) {
        scope_str = "global";
        reg_info = debug->main_function->reg_info[reg_num];
    }
    else {
        scope_str = "local";
        reg_info = debug->current_function->reg_info[reg_num];
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
        if (reg_info.line_num != 0)
            lily_msgbuf_add_fmt(msgbuf, " (%s from line %d)\n", reg_info.name,
                             reg_info.line_num);
        else
            lily_msgbuf_add_fmt(msgbuf, " (%s [builtin])\n", reg_info.name);
    }
    else
        lily_msgbuf_add(msgbuf, "\n");

    write_msgbuf(debug);
}

/*  show_code
    Show the code inside of a function. This uses optable and opcode_names to
    assist in showing code information. */
static void show_code(lily_debug_state *debug)
{
    char format[5];
    int digits, i, len;
    uint16_t *code;
    lily_msgbuf *msgbuf = debug->msgbuf;
    void *data = debug->data;

    digits = 0;
    i = 0;
    code = debug->current_function->code;
    len = debug->current_function->pos;

    while (len) {
        len /= 10;
        digits++;
    }

    len = debug->current_function->pos;
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

        const int *opcode_data = optable[opcode];
        char *opcode_name = opcode_names[opcode];
        int call_type = 0, count = 0, data_code, is_global = 0, j;

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

        for (j = 1;j <= opcode_data[1];j++) {
            data_code = opcode_data[j+1];

            if (data_code == D_LINENO)
                lily_impl_puts(data, "\n");
            else if (data_code == D_INPUT)
                show_register_info(debug, RI_INPUT, code[i+j]);
            else if (data_code == D_OUTPUT) {
                /* output is NULL if it's a function that does not return a
                   value. Omit this for brevity (the lack of a stated output
                   meaning it doesn't have one). */
                if ((int16_t)code[i+j] != -1)
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
                        indent, (int)code[i+j]);
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

                show_literal(debug, code[i+j]);
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
            else if (data_code == D_COND_INPUT) {
                int flags = RI_INPUT;
                if (is_global)
                    flags |= RI_GLOBAL;

                show_register_info(debug, flags, code[i+j]);
            }
            else if (data_code == D_CALL_TYPE)
                call_type = code[i+j];
            else if (data_code == D_CALL_INPUT) {
                if (call_type == 1)
                    show_function(debug, code[i+j]);
                else
                    show_register_info(debug, RI_INPUT, code[i+j]);
            }
            else if (data_code == D_FUNC_INPUT)
                show_function(debug, code[i+j]);
        }
        i += j;
    }
}

static void show_value(lily_debug_state *debug, lily_value *);

/*  show_instance_helper
    Recursively go through a given value, showing the properties that it
    contains (as well as the class that each came from). */
static void show_instance_helper(lily_debug_state *debug, lily_class *cls,
        lily_instance_val *ival, int *i, int *depth)
{
    if (cls->parent != NULL) {
        *depth = *depth + 1;
        show_instance_helper(debug, cls->parent, ival, i, depth);
        *depth = *depth - 1;
    }

    lily_prop_entry *prop_iter = cls->properties;
    int indent = debug->indent;
    lily_msgbuf *msgbuf = debug->msgbuf;

    if (prop_iter && *depth != 0)
        lily_msgbuf_add_fmt(msgbuf, "^I|____ From %s:\n",
                indent - 1, cls->name);

    while (prop_iter) {
        lily_msgbuf_add_fmt(msgbuf, "^I|____[(%d) %s] = ", indent,
                *i, prop_iter->name);
        write_msgbuf(debug);
        debug->indent++;
        show_value(debug, ival->values[*i]);
        debug->indent--;

        *i = *i + 1;
        prop_iter = prop_iter->next;
    }
}

/* These next three all handle dumps of instance, list (and tuple), and hash
   values. The concept is the same, but the contents are a different enough
   each time to require different functions. */

static void show_instance_value(lily_debug_state *debug, lily_sig *sig,
        lily_instance_val *ival)
{
    lily_msgbuf *msgbuf = debug->msgbuf;

    /* This intentionally dives into circular refs so that (circular) can be
       written with proper indentation. */
    if (ival->visited) {
        lily_msgbuf_add_fmt(msgbuf, "^I(circular)\n");
        write_msgbuf(debug);
        return;
    }

    ival->visited = 1;
    int i = 0;
    int depth = 0;

    show_instance_helper(debug, sig->cls, ival, &i, &depth);
    ival->visited = 0;
}

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

/*  show_value
    Recursively show a value given.
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

    if (cls_id == SYM_CLASS_STRING ||
        cls_id == SYM_CLASS_INTEGER ||
        cls_id == SYM_CLASS_DOUBLE) {
        show_simple_value(debug, sig, raw_value);
        lily_impl_puts(debug->data, "\n");
    }
    else if (cls_id == SYM_CLASS_LIST ||
             cls_id == SYM_CLASS_HASH ||
             cls_id == SYM_CLASS_TUPLE ||
             cls_id >= SYM_CLASS_EXCEPTION) {
        lily_msgbuf_add_fmt(debug->msgbuf, "^T\n", sig);
        write_msgbuf(debug);

        debug->indent++;
        if (cls_id == SYM_CLASS_HASH)
            show_hash_value(debug, sig, raw_value.hash);
        else if (cls_id < SYM_CLASS_EXCEPTION)
            show_list_value(debug, sig, raw_value.list);
        else
            show_instance_value(debug, sig, raw_value.instance);

        debug->indent--;
        /* The \n at the end comes from the last value's \n. */
    }
    else if (cls_id == SYM_CLASS_FUNCTION) {
        lily_function_val *fv = raw_value.function;

        lily_msgbuf_add_fmt(debug->msgbuf, "^T %s\n", sig, fv->trace_name);
        write_msgbuf(debug);

        if (fv->foreign_func == NULL) {
            lily_function_val *save_current;

            save_current = debug->current_function;
            debug->current_function = fv;
            show_code(debug);
            debug->current_function = save_current;
            /* The \n at the end comes from show_code always finishing that way. */
        }
    }
    else if (cls_id == SYM_CLASS_ANY) {
        lily_value *any_value = raw_value.any->inner_value;

        /* Don't condense, or it'll be (any) (nil), which seems a bit
           strange. */
        if (any_value->flags & VAL_IS_NIL)
            lily_impl_puts(debug->data, "(nil)\n");
        else {
            lily_impl_puts(debug->data, "(any) ");
            show_value(debug, any_value);
        }
    }
}

/** API for lily_debug.c **/
/*  lily_show_value
    The vm calls this for values given to 'show'. */
void lily_show_value(lily_vm_state *vm, lily_value *value)
{
    lily_debug_state debug;

    lily_vm_stack_entry **vm_stack = vm->function_stack;
    int stack_top = vm->function_stack_pos - 1;

    debug.indent = 0;
    debug.main_function = vm_stack[0]->function;
    debug.current_function = vm_stack[stack_top]->function;
    debug.msgbuf = vm->raiser->msgbuf;
    debug.vm = vm;
    debug.data = vm->data;

    lily_impl_puts(debug.data, "Value: ");
    show_value(&debug, value);
}
