#include <inttypes.h>
#include <string.h>

#include "lily_impl.h"
#include "lily_msgbuf.h"
#include "lily_core_types.h"
#include "lily_opcode.h"
#include "lily_opcode_table.h"
#include "lily_vm.h"

/** Debug is responsible for pretty printing whatever value it's given. This
    may entail functions, lists, hashes, and more. For native functions, the
    code inside of the function gets dumped. This is used to determine if an
    error is within the emitter's code generation or the vm. **/

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

typedef struct lily_debug_state_ {
    lily_function_val *main_function;
    lily_function_val *current_function;
    lily_msgbuf *msgbuf;
    lily_vm_state *vm;
    void *data;
    uint32_t indent;
    uint32_t pad;
} lily_debug_state;

/* Opcodes that have line numbers also have extra space so they print the line
   number at an even spot. This saves debug from having to calculate how much
   (and possibly getting it wrong) at the cost of a little bit of memory.
   No extra space means it doesn't have a line number. */
char *opcode_names[] = {
    "fast assign",
    "assign",
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
    "for (integer range)",
    "for setup",
    "get item",
    "set item",
    "get global",
    "set global",
    "get readonly",
    "get property",
    "set property",
    "push try",
    "pop try",
    "except",
    "raise",
    "setup optargs",
    "new instance",
    "match dispatch",
    "variant decompose",
    "get upvalue",
    "set upvalue",
    "create closure",
    "create function",
    "load class closure",
    "load closure",
    "return from vm"
};

static void write_msgbuf(lily_debug_state *debug)
{
    lily_impl_puts(debug->data, debug->msgbuf->message);
    lily_msgbuf_flush(debug->msgbuf);
}

/*  show_simple_value
    Show an integer, double, or string literal. */
static void show_simple_value(lily_debug_state *debug, lily_type *type,
        lily_raw_value value)
{
    lily_value v = {0, type, value};

    lily_msgbuf_add_simple_value(debug->msgbuf, &v);
    write_msgbuf(debug);
}

/*  show_function
    Show the name of a literal|function at the given position. */
static void show_readonly(lily_debug_state *debug, int position)
{
    lily_tie *tie = debug->vm->readonly_table[position];
    if (tie->type->cls->id == SYM_CLASS_FUNCTION) {
        lily_function_val *func_val = tie->value.function;

        lily_msgbuf_add_fmt(debug->msgbuf, "^I|     <---- (^T) ", debug->indent,
                tie->type);

        if (func_val->class_name != NULL)
            lily_msgbuf_add_fmt(debug->msgbuf, "%s::", func_val->class_name);

        if (func_val->line_num != 0)
            lily_msgbuf_add_fmt(debug->msgbuf, "%s from line %d\n",
                    func_val->trace_name, func_val->line_num);
        else
            lily_msgbuf_add_fmt(debug->msgbuf, "%s [builtin]\n",
                    func_val->trace_name);

        write_msgbuf(debug);
    }
    else {
        lily_tie *lit = debug->vm->readonly_table[position];
        lily_value v = {0, lit->type, lit->value};
        lily_msgbuf_add_fmt(debug->msgbuf, "^I|     <---- (^T) ^V\n",
                debug->indent, lit->type, &v);
        write_msgbuf(debug);
    }
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
            debug->indent, arrow_str, reg_info.type, scope_str, reg_num);

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
    Show the code inside of a function. This uses opcode_table and opcode_names
    to assist in showing code information. */
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
    len = debug->current_function->len;

    while (len) {
        len /= 10;
        digits++;
    }

    len = debug->current_function->len;
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

        const int *opcode_data = opcode_table[opcode];
        char *opcode_name = opcode_names[opcode];
        int call_type = 0, count = 0, data_code, j;
        lily_class *match_cls = NULL;

        /* Group under a new line number if the current one isn't the last one
           seen. This makes it easy to see what operations that are caused by
           a particular line number. After that, the [] indicates the position
           of i for extra debugging. */
        if (opcode_data[1] == C_LINENO && code[i+1] != last_line_num) {
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
           These two, however, do not, so insert one in. */
        if (opcode == o_jump || opcode == o_create_function)
            lily_impl_puts(data, "\n");

        for (j = 1;j <= opcode_data[1];j++) {
            data_code = opcode_data[j+1];

            if (data_code == C_LINENO)
                lily_impl_puts(data, "\n");
            else if (data_code == C_INPUT)
                show_register_info(debug, RI_INPUT, code[i+j]);
            else if (data_code == C_OUTPUT) {
                /* output is NULL if it's a function that does not return a
                   value. Omit this for brevity (the lack of a stated output
                   meaning it doesn't have one). */
                if ((int16_t)code[i+j] != -1)
                    show_register_info(debug, RI_OUTPUT, code[i+j]);
            }
            else if (data_code == C_JUMP_ON) {
                if (code[i+j] == 0)
                    lily_impl_puts(data, " false\n");
                else
                    lily_impl_puts(data, " true\n");
            }
            else if (data_code == C_JUMP) {
                lily_msgbuf_add_fmt(msgbuf, "^I|     -> | [%d]\n",
                        indent, (int)code[i+j]);
                write_msgbuf(debug);
            }
            else if (data_code == C_COUNT)
                count = (int)code[i+j];
            else if (data_code == C_COUNT_LIST) {
                if (count == 0)
                    i--;
                else {
                    int k;
                    for (k = 0;k < count;k++, i++)
                        show_register_info(debug, RI_INPUT, code[i+j]);

                    i--;
                }
            }
            else if (data_code == C_COUNT)
                count = (int)code[i+j];
            else if (data_code == C_INT_VAL) {
                lily_msgbuf_add_fmt(msgbuf, "^I|     <---- %d\n",
                        indent, (int)code[i+j]);
                write_msgbuf(debug);
            }
            else if (data_code == C_NOP) {
                lily_impl_puts(data, "\n");
                break;
            }
            else if (data_code == C_GLOBAL_INPUT)
                show_register_info(debug, RI_GLOBAL | RI_INPUT, code[i+j]);
            else if (data_code == C_GLOBAL_OUTPUT) {
                /* This doesn't have to be checked because C_GLOBAL_OUTPUT is
                   only for writes to a global, and always exists. */
                show_register_info(debug, RI_GLOBAL | RI_OUTPUT, code[i+j]);
            }
            else if (data_code == C_CALL_TYPE)
                call_type = code[i+j];
            else if (data_code == C_CALL_INPUT) {
                if (call_type == 1)
                    show_readonly(debug, code[i+j]);
                else
                    show_register_info(debug, RI_INPUT, code[i+j]);
            }
            else if (data_code == C_READONLY_INPUT)
                show_readonly(debug, code[i+j]);
            else if (data_code == C_MATCH_INPUT) {
                show_register_info(debug, RI_INPUT, code[i+j]);
                match_cls = debug->current_function->reg_info[code[i+j]].type->cls;
            }
            /* This is for o_match_dispatch, and each of these cases
               corresponds to a variant classes. To help with debugging, show
               where the different classes will jump to. */
            else if (data_code == C_COUNT_JUMPS) {
                int k;
                for (k = 0;k < count;k++, i++) {
                    lily_msgbuf_add_fmt(msgbuf, "^I|     -> | [%d] (case: %s)\n",
                            indent, (int)code[i+j],
                            match_cls->variant_members[k]->name);
                    write_msgbuf(debug);
                }

                i--;
            }
            /* This is currently only used by o_variant_decompose, so there's
               no need to check for -1 like in the normal output handler since
               that isn't possible. */
            else if (data_code == C_COUNT_OUTPUTS) {
                int k;
                for (k = 0;k < count;k++, i++)
                    show_register_info(debug, RI_OUTPUT, code[i+j]);

                i--;
            }
            else if (data_code == C_COUNT_OPTARGS) {
                /* Must be done, because there was no linenum for this. */
                lily_msgbuf_add_char(msgbuf, '\n');

                count = code[i + j];
                i++;
                int k, half = count / 2;
                /* Writing them in pairs makes it more obvious where each value
                   is going to end up. */
                for (k = 0;k < half;k++) {
                    show_readonly(debug, code[i+j+k]);
                    show_register_info(debug, RI_OUTPUT, code[i+j+k+half]);
                }
                i += count - 1;
            }
        }
        i += j;
    }
}

static void show_value(lily_debug_state *debug, lily_value *);

static void show_property(lily_debug_state *debug, lily_prop_entry *prop,
        lily_instance_val *ival)
{
    if (prop->next)
        show_property(debug, prop->next, ival);

    lily_msgbuf_add_fmt(debug->msgbuf, "^I|____[(%d) %s] = ", debug->indent,
            prop->id, prop->name);
    write_msgbuf(debug);
    debug->indent++;

    /* 'show(self)' can be done before all of the properties are all declared.
       Trying to restrict that is...far too difficult to be considered. Instead,
       simply prepare for unset instance properties (they aren't accessible
       before being declared, so it's fine)... */
    if ((ival->values[prop->id]->flags & VAL_IS_NIL) == 0)
        show_value(debug, ival->values[prop->id]);
    else {
        lily_msgbuf_add(debug->msgbuf, "?\n");
        write_msgbuf(debug);
    }
    debug->indent--;
}

/*  show_instance_helper
    Recursively go through a given value, showing the properties that it
    contains (as well as the class that each came from). */
static void show_instance_helper(lily_debug_state *debug, lily_class *cls,
        lily_instance_val *ival, int depth)
{
    if (cls->parent)
        show_instance_helper(debug, cls->parent, ival, depth + 1);

    lily_prop_entry *prop = cls->properties;

    if (prop) {
        if (depth || cls->parent)
            lily_msgbuf_add_fmt(debug->msgbuf, "^I|____ From %s:\n",
                    debug->indent - 1, cls->name);

        show_property(debug, prop, ival);
    }
}

/* These next three all handle dumps of instance, list (and tuple), and hash
   values. The concept is the same, but the contents are a different enough
   each time to require different functions. */

static void show_instance_value(lily_debug_state *debug, lily_type *type,
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
    int depth = 0;

    show_instance_helper(debug, type->cls, ival, depth);
    ival->visited = 0;
}

static void show_list_value(lily_debug_state *debug, lily_type *type,
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

static void show_hash_value(lily_debug_state *debug, lily_type *type,
        lily_hash_val *hash_val)
{
    int indent;
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
    elem_iter = hash_val->elem_chain;
    while (elem_iter) {
        /* Write out one blank line, so each value has a space between the next.
           This keeps things from appearing crammed together.
           Not needed for the first line though. */
        if (elem_iter != hash_val->elem_chain)
            lily_msgbuf_add_fmt(msgbuf, "^I|\n", indent - 1);

        lily_msgbuf_add_fmt(msgbuf, "^I|____[^V] = ", indent - 1,
                elem_iter->elem_key);
        write_msgbuf(debug);
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
    lily_type *type = value->type;
    int cls_id = type->cls->id;
    lily_raw_value raw_value = value->value;

    if (value->flags & VAL_IS_NIL) {
        lily_impl_puts(debug->data, "(nil)\n");
        return;
    }

    if (cls_id == SYM_CLASS_STRING ||
        cls_id == SYM_CLASS_INTEGER ||
        cls_id == SYM_CLASS_DOUBLE ||
        cls_id == SYM_CLASS_BYTESTRING) {
        show_simple_value(debug, type, raw_value);
        lily_impl_puts(debug->data, "\n");
    }
    else if (cls_id == SYM_CLASS_LIST ||
             cls_id == SYM_CLASS_HASH ||
             cls_id == SYM_CLASS_TUPLE ||
             (cls_id >= SYM_CLASS_EXCEPTION &&
              (type->cls->flags & CLS_ENUM_CLASS) == 0)) {
        lily_msgbuf_add_fmt(debug->msgbuf, "^T\n", type);
        write_msgbuf(debug);

        debug->indent++;
        if (cls_id == SYM_CLASS_HASH)
            show_hash_value(debug, type, raw_value.hash);
        else if (cls_id < SYM_CLASS_EXCEPTION)
            show_list_value(debug, type, raw_value.list);
        else if (type->cls->flags & CLS_VARIANT_CLASS) {
            if (type->cls->variant_type->subtype_count != 0)
                show_list_value(debug, type, raw_value.list);
            /* else it's a variant that does not take any arguments, so do
               nothing because nothing to show. */
        }
        else
            show_instance_value(debug, type, raw_value.instance);

        debug->indent--;
        /* The \n at the end comes from the last value's \n. */
    }
    else if (cls_id == SYM_CLASS_FUNCTION) {
        lily_function_val *fv = raw_value.function;

        lily_msgbuf_add_fmt(debug->msgbuf, "^T %s\n", type, fv->trace_name);
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
    else if (cls_id == SYM_CLASS_ANY ||
             type->cls->flags & CLS_ENUM_CLASS) {
        lily_value *any_value = raw_value.any->inner_value;

        lily_msgbuf_add_fmt(debug->msgbuf, "(^T) ", type);
        write_msgbuf(debug);
        show_value(debug, any_value);
    }
}

/** API for lily_debug.c **/
/*  lily_show_value
    The vm calls this for values given to 'show'. */
void lily_show_value(lily_vm_state *vm, lily_value *value)
{
    lily_debug_state debug;

    debug.indent = 0;
    debug.main_function = vm->symtab->main_function;
    debug.current_function = vm->call_chain->function;
    debug.msgbuf = vm->raiser->msgbuf;
    debug.vm = vm;
    debug.data = vm->data;

    /* Make sure this is clear so output from it doesn't show at the front of
       everything. */
    lily_msgbuf_flush(debug.msgbuf);

    lily_impl_puts(debug.data, "Value: ");
    show_value(&debug, value);
}
