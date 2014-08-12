#include <stddef.h>
#include <string.h>
#include <inttypes.h>

#include "lily_impl.h"
#include "lily_value.h"
#include "lily_opcode.h"
#include "lily_vm.h"
#include "lily_debug.h"
#include "lily_gc.h"
extern uint64_t siphash24(const void *src, unsigned long src_sz, const char key[16]);

/* LOAD_CHECKED_REG is used to load a register and check it for not having a
   value. Using this macro ensures that novalue_error will be called with the
   correct index (since it's given the same index as the code position).
   Arguments are:
   * load_reg:      The register to load the value into.
   * load_code_pos: The current code position. In the vm, this is always
                    code_pos.
   * load_pos:      How far after load_code_pos to look for the register value.
                    This is also used by novalue_error to locate the register in
                    case there is an error.

   This macro is the preferred way of checking for something being nil because
   it ensures that novalue_error gets the correct index. */
#define LOAD_CHECKED_REG(load_reg, load_code_pos, load_pos) \
load_reg = vm_regs[code[load_code_pos + load_pos]]; \
if (load_reg->flags & VAL_IS_NIL) \
    novalue_error(vm, load_code_pos, load_pos); \

#define INTEGER_OP(OP) \
LOAD_CHECKED_REG(lhs_reg, code_pos, 2) \
LOAD_CHECKED_REG(rhs_reg, code_pos, 3) \
vm_regs[code[code_pos+4]]->value.integer = \
lhs_reg->value.integer OP rhs_reg->value.integer; \
vm_regs[code[code_pos+4]]->flags &= ~VAL_IS_NIL; \
code_pos += 5;

#define INTDBL_OP(OP) \
LOAD_CHECKED_REG(lhs_reg, code_pos, 2) \
LOAD_CHECKED_REG(rhs_reg, code_pos, 3) \
if (lhs_reg->sig->cls->id == SYM_CLASS_DOUBLE) { \
    if (rhs_reg->sig->cls->id == SYM_CLASS_DOUBLE) \
        vm_regs[code[code_pos+4]]->value.doubleval = \
        lhs_reg->value.doubleval OP rhs_reg->value.doubleval; \
    else \
        vm_regs[code[code_pos+4]]->value.doubleval = \
        lhs_reg->value.doubleval OP rhs_reg->value.integer; \
} \
else \
    vm_regs[code[code_pos+4]]->value.doubleval = \
    lhs_reg->value.integer OP rhs_reg->value.doubleval; \
vm_regs[code[code_pos+4]]->flags &= ~VAL_IS_NIL; \
code_pos += 5;

/* EQUALITY_COMPARE_OP is used for == and !=, instead of a normal COMPARE_OP.
   The difference is that this will allow op on any type, so long as the lhs
   and rhs agree on the full type. This allows comparing functions, hashes
   lists, and more.

   Arguments are:
   * op:       The operation to perform relative to the values given. This will
               be substituted like: lhs->value OP rhs->value
               This is done for everything BUT string.
   * stringop: The operation to perform relative to the result of strcmp. ==
               does == 0, as an example. */
#define EQUALITY_COMPARE_OP(OP, STRINGOP) \
LOAD_CHECKED_REG(lhs_reg, code_pos, 2) \
LOAD_CHECKED_REG(rhs_reg, code_pos, 3) \
if (lhs_reg->sig->cls->id == SYM_CLASS_DOUBLE) { \
    if (rhs_reg->sig->cls->id == SYM_CLASS_DOUBLE) \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.doubleval OP rhs_reg->value.doubleval); \
    else \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.doubleval OP rhs_reg->value.integer); \
} \
else if (lhs_reg->sig->cls->id == SYM_CLASS_INTEGER) { \
    if (rhs_reg->sig->cls->id == SYM_CLASS_INTEGER) \
        vm_regs[code[code_pos+4]]->value.integer =  \
        (lhs_reg->value.integer OP rhs_reg->value.integer); \
    else \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.integer OP rhs_reg->value.doubleval); \
} \
else if (lhs_reg->sig->cls->id == SYM_CLASS_STRING) { \
    vm_regs[code[code_pos+4]]->value.integer = \
    strcmp(lhs_reg->value.string->string, \
           rhs_reg->value.string->string) STRINGOP; \
} \
else if (lhs_reg->sig == rhs_reg->sig) { \
    vm_regs[code[code_pos+4]]->value.integer = \
    compare_values(vm, lhs_reg, rhs_reg) OP 1; \
} \
vm_regs[code[code_pos+4]]->flags &= ~VAL_IS_NIL; \
code_pos += 5;

#define COMPARE_OP(OP, STRINGOP) \
LOAD_CHECKED_REG(lhs_reg, code_pos, 2) \
LOAD_CHECKED_REG(rhs_reg, code_pos, 3) \
if (lhs_reg->sig->cls->id == SYM_CLASS_DOUBLE) { \
    if (rhs_reg->sig->cls->id == SYM_CLASS_DOUBLE) \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.doubleval OP rhs_reg->value.doubleval); \
    else \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.doubleval OP rhs_reg->value.integer); \
} \
else if (lhs_reg->sig->cls->id == SYM_CLASS_INTEGER) { \
    if (rhs_reg->sig->cls->id == SYM_CLASS_INTEGER) \
        vm_regs[code[code_pos+4]]->value.integer =  \
        (lhs_reg->value.integer OP rhs_reg->value.integer); \
    else \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.integer OP rhs_reg->value.doubleval); \
} \
else if (lhs_reg->sig->cls->id == SYM_CLASS_STRING) { \
    vm_regs[code[code_pos+4]]->value.integer = \
    strcmp(lhs_reg->value.string->string, \
           rhs_reg->value.string->string) STRINGOP; \
} \
vm_regs[code[code_pos+4]]->flags &= ~VAL_IS_NIL; \
code_pos += 5;

/** vm init and deletion **/
lily_vm_state *lily_new_vm_state(lily_raiser *raiser, void *data)
{
    lily_vm_state *vm = lily_malloc(sizeof(lily_vm_state));
    if (vm == NULL)
        return NULL;

    /* todo: This is a terrible, horrible key to use. Make a better one using
             some randomness or...something. Just not this. */
    char sipkey[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
    lily_vm_stringbuf *stringbuf = lily_malloc(sizeof(lily_vm_stringbuf));
    char *string_data = lily_malloc(64);

    vm->function_stack = lily_malloc(sizeof(lily_vm_stack_entry *) * 4);
    vm->sipkey = lily_malloc(16);
    vm->foreign_code = lily_malloc(sizeof(uintptr_t));
    vm->string_buffer = NULL;
    vm->function_stack_pos = 0;
    vm->raiser = raiser;
    vm->data = data;
    vm->main = NULL;
    vm->vm_regs = NULL;
    vm->regs_from_main = NULL;
    vm->num_registers = 0;
    vm->max_registers = 0;
    vm->gc_live_entries = NULL;
    vm->gc_spare_entries = NULL;
    vm->gc_live_entry_count = 0;
    vm->gc_threshold = 100;
    vm->gc_pass = 0;
    vm->prep_id_start = 0;
    vm->prep_var_start = NULL;

    if (vm->function_stack) {
        int i;
        for (i = 0;i < 4;i++) {
            vm->function_stack[i] = lily_malloc(sizeof(lily_vm_stack_entry));
            if (vm->function_stack[i] == NULL)
                break;
        }
        vm->function_stack_size = i;
    }
    else
        vm->function_stack_size = 0;

    if (vm->sipkey)
        memcpy(vm->sipkey, sipkey, 16);

    if (vm->function_stack == NULL || vm->function_stack_size != 4 ||
        vm->sipkey == NULL || stringbuf == NULL || string_data == NULL ||
        vm->foreign_code == NULL) {
        lily_free(stringbuf);
        lily_free(string_data);
        lily_free_vm_state(vm);
        return NULL;
    }

    stringbuf->data = string_data;
    stringbuf->data_size = 64;
    vm->string_buffer = stringbuf;
    vm->foreign_code[0] = o_return_from_vm;

    return vm;
}

void lily_vm_free_registers(lily_vm_state *vm)
{
    lily_value **regs_from_main = vm->regs_from_main;
    lily_value *reg;
    int i;
    for (i = vm->max_registers-1;i >= 0;i--) {
        reg = regs_from_main[i];

        if (reg->sig->cls->is_refcounted &&
            (reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
            lily_deref_unknown_val(reg);

        lily_free(reg);
    }

    /* This keeps the final gc invoke from touching the now-deleted registers.
       It also ensures the last invoke will get everything. */
    vm->num_registers = 0;
    vm->max_registers = 0;

    lily_free(regs_from_main);
}

void lily_vm_invoke_gc(lily_vm_state *);
void lily_vm_destroy_gc(lily_vm_state *);

void lily_free_vm_state(lily_vm_state *vm)
{
    int i;
    for (i = 0;i < vm->function_stack_size;i++)
        lily_free(vm->function_stack[i]);

    /* Force an invoke, even if it's not time. This should clear everything that
       is still left. */
    lily_vm_invoke_gc(vm);

    lily_vm_destroy_gc(vm);

    if (vm->string_buffer) {
        lily_free(vm->string_buffer->data);
        lily_free(vm->string_buffer);
    }

    lily_free(vm->foreign_code);
    lily_free(vm->sipkey);
    lily_free(vm->function_stack);
    lily_free(vm);
}

void lily_vm_destroy_gc(lily_vm_state *vm)
{
    lily_gc_entry *gc_iter, *gc_temp;
    gc_iter = vm->gc_live_entries;

    while (gc_iter != NULL) {
        gc_temp = gc_iter->next;

        lily_free(gc_iter);

        gc_iter = gc_temp;
    }

    gc_iter = vm->gc_spare_entries;
    while (gc_iter != NULL) {
        gc_temp = gc_iter->next;

        lily_free(gc_iter);

        gc_iter = gc_temp;
    }
}

/*  lily_vm_invoke_gc
    This is Lily's garbage collector. It runs in multiple stages:
    1: Go to each _in-use_ register that is not nil and use the appropriate
       gc_marker call to mark all values inside that value which are visible.
       Visible items are set to the vm's ->gc_pass.
    2: Go through all the gc items now. Anything which doesn't have the current
       pass as its last_pass is considered unreachable. This will deref values
       that cannot be circular, or forcibly collect possibly-circular values.
       Caveats:
       * Some gc_entries may have their value set to 0/NULL. This happens when
         a possibly-circular value has been deleted through typical ref/deref
         means.
       * The lily_gc_collect_* will collect everything inside a non-circular
         value, but not the value itself. It will set last_pass to -1 when it
         does this. This is necessary because it's possible that a value may be
         sent to lily_gc_collect_* calls multiple times (because circular
         references). If it's deleted, then there will be invalid reads.
    3: Stage 1 skipped registers that are not in-use, because Lily just hasn't
       gotten around to clearing them yet. However, some of those registers may
       contain a value that has a gc_entry that indicates that the value is to
       be destroyed. It's -very- important that these registers be marked as nil
       so that prep_registers will not try to deref a value that has been
       destroyed by the gc.
    4: Finally, destroy the lists, anys, etc. that stage 2 didn't clear.
       Absolutely nothing is using these now, so it's safe to destroy them.

    vm: The vm to invoke the gc of. */
void lily_vm_invoke_gc(lily_vm_state *vm)
{
    /* This is (sort of) a mark-and-sweep garbage collector. This is called when
       a certain number of allocations have been done. Take note that values
       can be destroyed by deref. However, those values will have the gc_entry's
       value set to NULL as an indicator. */
    vm->gc_pass++;

    lily_value **regs_from_main = vm->regs_from_main;
    int pass = vm->gc_pass;
    int num_registers = vm->num_registers;
    int i;
    lily_gc_entry *gc_iter;

    /* Stage 1: Go through all registers and use the appropriate gc_marker call
                that will mark every inner value that's visible. */
    for (i = 0;i < vm->num_registers;i++) {
        lily_value *reg = regs_from_main[i];
        if ((reg->sig->flags & SIG_MAYBE_CIRCULAR) &&
            (reg->flags & VAL_IS_NIL) == 0 &&
             reg->value.gc_generic->gc_entry != NULL) {
            (*reg->sig->cls->gc_marker)(pass, reg);
        }
    }

    /* Stage 2: Start destroying everything that wasn't marked as visible.
                Don't forget to check ->value for NULL in case the value was
                destroyed through normal ref/deref means. */
    for (gc_iter = vm->gc_live_entries;
         gc_iter;
         gc_iter = gc_iter->next) {
        if (gc_iter->last_pass != pass &&
            gc_iter->value.generic != NULL) {
            lily_gc_collect_value(gc_iter->value_sig, gc_iter->value);
        }
    }

    /* num_registers is -1 if the vm is calling this from lily_free_vm_state and
       there are no registers left. */
    if (num_registers != -1) {
        int i;
        /* Stage 3: Check registers not currently in use to see if they hold a
                    value that's going to be collected. If so, then mark the
                    register as nil so that */
        for (i = vm->num_registers;i < vm->max_registers;i++) {
            lily_value *reg = regs_from_main[i];
            if ((reg->sig->flags & SIG_MAYBE_CIRCULAR) &&
                (reg->flags & VAL_IS_NIL) == 0 &&
                /* Not sure if this next line is necessary though... */
                reg->value.gc_generic->gc_entry != NULL &&
                reg->value.gc_generic->gc_entry->last_pass == -1) {
                reg->flags |= VAL_IS_NIL;
            }
        }
    }

    /* Stage 4: Delete the lists/anys/etc. that stage 2 didn't delete.
                Nothing is using them anymore. Also, sort entries into those
                that are living and those that are no longer used. */
    i = 0;
    lily_gc_entry *new_live_entries = NULL;
    lily_gc_entry *new_spare_entries = vm->gc_spare_entries;
    lily_gc_entry *iter_next = NULL;
    gc_iter = vm->gc_live_entries;

    while (gc_iter) {
        iter_next = gc_iter->next;
        i++;

        if (gc_iter->last_pass == -1) {
            lily_free(gc_iter->value.generic);

            gc_iter->next = new_spare_entries;
            new_spare_entries = gc_iter;
        }
        else {
            gc_iter->next = new_live_entries;
            new_live_entries = gc_iter;
        }

        gc_iter = iter_next;
    }

    vm->gc_live_entry_count = i;
    vm->gc_live_entries = new_live_entries;
    vm->gc_spare_entries = new_spare_entries;
}

/*  lily_try_add_gc_item

    This attempts to get a lily_gc_entry for the given value. This may call the
    gc into action if there are vm->gc_threshold entries in vm->gc_live_entries
    before the attempt to get a new value.

    Take note, the gc may be invoked regardless of what this call returns.

    vm:        This is sent in case the gc needs to be collected. The new
               gc entry is also added to the vm's ->gc_live_entries.
    value_sig: The sig describing the value given.
    value:     The value to attach a gc_entry to. This can be any lily_value
               that is a superset of lily_generic_gc_val.

    Returns 1 if successful, 0 otherwise. Additionally, the value's ->gc_entry
    is set to the new gc_entry on success. */
int lily_try_add_gc_item(lily_vm_state *vm, lily_sig *value_sig,
        lily_generic_gc_val *value)
{
    /* The given value is likely not in a register, so run the gc before adding
       the value to an entry. Otherwise, the value will be destroyed if the gc
       runs. */
    if (vm->gc_live_entry_count >= vm->gc_threshold)
        lily_vm_invoke_gc(vm);

    lily_gc_entry *new_entry;
    if (vm->gc_spare_entries != NULL) {
        new_entry = vm->gc_spare_entries;
        vm->gc_spare_entries = vm->gc_spare_entries->next;
    }
    else {
        new_entry = lily_malloc(sizeof(lily_gc_entry));
        if (new_entry == NULL)
            return 0;
    }

    new_entry->value_sig = value_sig;
    new_entry->value.gc_generic = value;
    new_entry->last_pass = 0;

    new_entry->next = vm->gc_live_entries;
    vm->gc_live_entries = new_entry;

    /* Attach the gc_entry to the value so the caller doesn't have to. */
    value->gc_entry = new_entry;
    vm->gc_live_entry_count++;

    return 1;
}

/** VM helpers **/

/*  compare_values
    This is a helper function to call the class_eq_func on the values given.
    The vm is passed in case an error needs to be raised. */
static int compare_values(lily_vm_state *vm, lily_value *left, lily_value *right)
{
    class_eq_func eq_func = left->sig->cls->eq_func;
    int depth = 0;

    int result = eq_func(vm, &depth, left, right);
    return result;
}

/* grow_function_stack
   This function grows the vm's function stack so it can take more function
   info. Calls lily_raise_nomem if unable to create function info. */
static void grow_function_stack(lily_vm_state *vm)
{
    int i;
    lily_vm_stack_entry **new_stack;

    /* Functions are free'd from 0 to stack_size, so don't increase stack_size
       just yet. */
    new_stack = lily_realloc(vm->function_stack,
            sizeof(lily_vm_stack_entry *) * 2 * vm->function_stack_size);

    if (new_stack == NULL)
        lily_raise_nomem(vm->raiser);

    vm->function_stack = new_stack;
    vm->function_stack_size *= 2;

    for (i = vm->function_stack_pos+1;i < vm->function_stack_size;i++) {
        vm->function_stack[i] = lily_malloc(sizeof(lily_vm_stack_entry));
        if (vm->function_stack[i] == NULL) {
            vm->function_stack_size = i;
            lily_raise_nomem(vm->raiser);
        }
    }
}

/* maybe_crossover_assign
   This handles assignment between two symbols which don't have the exact same
   type. This assumes the caller has verified that rhs is not nil.
   Returns 1 if the assignment happened, 0 otherwise. */
int maybe_crossover_assign(lily_value *lhs_reg, lily_value *rhs_reg)
{
    int ret = 1;

    if (rhs_reg->sig->cls->id == SYM_CLASS_ANY)
        rhs_reg = rhs_reg->value.any->inner_value;

    if (lhs_reg->sig->cls->id == SYM_CLASS_INTEGER &&
        rhs_reg->sig->cls->id == SYM_CLASS_DOUBLE)
        lhs_reg->value.integer = (int64_t)(rhs_reg->value.doubleval);
    else if (lhs_reg->sig->cls->id == SYM_CLASS_DOUBLE &&
             rhs_reg->sig->cls->id == SYM_CLASS_INTEGER)
        lhs_reg->value.doubleval = (double)(rhs_reg->value.integer);
    else
        ret = 0;

    if (ret)
        lhs_reg->flags &= ~VAL_IS_NIL;

    return ret;
}

/* novalue_error
   This is a helper routine that raises ErrNoValue because the given sym is
   nil but should not be. code_pos is the current code position, because the
   current function's info is not saved in the stack (because it would almost
   always be stale). */
static void novalue_error(lily_vm_state *vm, int code_pos, int reg_pos)
{
    /* ...So fill in the current function's info before dying. */
    lily_vm_stack_entry *top = vm->function_stack[vm->function_stack_pos-1];
    /* Functions do not have a linetable that maps opcodes to line numbers.
       Instead, the emitter writes the line number right after the opcode for
       any opcode that might call novalue_error. */
    top->line_num = top->code[code_pos+1];

    /* Instead of using the register, grab the register info for the current
       function. This will have the name, if this particular register is used
       to hold a named var. */
    lily_register_info *reg_info;
    reg_info = vm->function_stack[vm->function_stack_pos - 1]->function->reg_info;

    /* A functions's register info and the registers are the same size. The
       info at position one is for the first register, the second for the
       second register, etc. */
    lily_register_info err_reg_info;
    err_reg_info = reg_info[top->code[code_pos+reg_pos]];

    /* If this register corresponds to a named value, show that. */
    if (err_reg_info.name != NULL)
        lily_raise(vm->raiser, lily_ErrNoValue, "%s is nil.\n",
                   err_reg_info.name);
    else
        lily_raise(vm->raiser, lily_ErrNoValue, "Attempt to use nil value.\n");
}

/*  no_such_key_error
    This is a helper routine that raises ErrNoSuchKey when there is an attempt
    to read a hash that does not have the given key.
    Note: This is intentionally not called by o_set_item so that assigning to a
          non-existant part of a hash automatically adds that key.

    vm:       The currently running vm.
    code_pos: The start of the opcode, for getting line info.
    key:      The invalid key passed. */
void no_such_key_error(lily_vm_state *vm, int code_pos, lily_value *key)
{
    lily_vm_stack_entry *top = vm->function_stack[vm->function_stack_pos - 1];
    top->line_num = top->code[code_pos + 1];

    lily_msgbuf *msgbuf = vm->raiser->msgbuf;
    int key_cls_id = key->sig->cls->id;

    lily_msgbuf_add(msgbuf, "ErrNoSuchKey: ");
    if (key_cls_id == SYM_CLASS_INTEGER)
        lily_msgbuf_add_int(msgbuf, key->value.integer);
    else if (key_cls_id == SYM_CLASS_DOUBLE)
        lily_msgbuf_add_double(msgbuf, key->value.doubleval);
    else if (key_cls_id == SYM_CLASS_STRING) {
        lily_msgbuf_add_char(msgbuf, '\"');
        /* Note: This is fine for now because strings can't contain \0. */
        lily_msgbuf_add(msgbuf, key->value.string->string);
        lily_msgbuf_add_char(msgbuf, '\"');
    }
    else
        lily_msgbuf_add(msgbuf, "? (unable to print key).");

    lily_raise_prebuilt(vm->raiser, lily_ErrNoSuchKey);
}

/* divide_by_zero_error
   This is copied from novalue_error, except it raises ErrDivisionByZero and
   reports an attempt to divide by zero. */
void divide_by_zero_error(lily_vm_state *vm, int code_pos, int reg_pos)
{
    lily_vm_stack_entry *top = vm->function_stack[vm->function_stack_pos-1];
    top->line_num = top->code[code_pos+1];

    lily_raise(vm->raiser, lily_ErrDivideByZero,
            "Attempt to divide by zero.\n");
}

/* boundary_error
   Another copy of novalue_error, this one raising ErrOutOfRange. */
void boundary_error(lily_vm_state *vm, int code_pos, int bad_index)
{
    lily_vm_stack_entry *top = vm->function_stack[vm->function_stack_pos-1];
    top->line_num = top->code[code_pos+1];

    lily_raise(vm->raiser, lily_ErrOutOfRange,
            "Subscript index %d is out of range.\n", bad_index);
}

/* lily_builtin_print
   This is called by the vm to implement the print function. [0] is the return
   (which isn't used), so args begin at [1]. */
void lily_builtin_print(lily_vm_state *vm, lily_function_val *self,
        uintptr_t *code)
{
    lily_value *reg = vm->vm_regs[code[0]];
    lily_impl_puts(vm->data, reg->value.string->string);
}

/* lily_builtin_printfmt
   This is called by the vm to implement the printfmt function. [0] is the
   return, which is ignored in this case. [1] is the format string, and [2]+
   are the arguments. */
void lily_builtin_printfmt(lily_vm_state *vm, lily_function_val *self,
        uintptr_t *code)
{
    char fmtbuf[64];
    char save_ch;
    char *fmt, *str_start;
    int cls_id;
    int arg_pos = 0, i = 0;
    lily_value **vm_regs = vm->vm_regs;
    lily_value *arg;
    lily_raw_value val;
    lily_list_val *vararg_lv;
    lily_any_val *arg_av;
    void *data = vm->data;

    fmt = vm_regs[code[0]]->value.string->string;
    vararg_lv = vm_regs[code[1]]->value.list;

    str_start = fmt;
    while (1) {
        if (fmt[i] == '\0')
            break;
        else if (fmt[i] == '%') {
            if (arg_pos == vararg_lv->num_values)
                lily_raise(vm->raiser, lily_ErrFormat,
                        "Not enough args for printfmt.\n");

            save_ch = fmt[i];
            fmt[i] = '\0';
            lily_impl_puts(data, str_start);
            fmt[i] = save_ch;
            i++;

            arg_av = vararg_lv->elems[arg_pos]->value.any;
            if (arg_av->inner_value->flags & VAL_IS_NIL)
                lily_raise(vm->raiser, lily_ErrFormat,
                        "Argument #%d to printfmt is nil.\n", arg_pos + 2);

            arg = arg_av->inner_value;
            cls_id = arg->sig->cls->id;
            val = arg->value;

            if (fmt[i] == 'i') {
                if (cls_id != SYM_CLASS_INTEGER)
                    return;
                snprintf(fmtbuf, 63, "%" PRId64, val.integer);
                lily_impl_puts(data, fmtbuf);
            }
            else if (fmt[i] == 's') {
                if (cls_id != SYM_CLASS_STRING)
                    return;
                else
                    lily_impl_puts(data, val.string->string);
            }
            else if (fmt[i] == 'd') {
                if (cls_id != SYM_CLASS_DOUBLE)
                    return;

                snprintf(fmtbuf, 63, "%f", val.doubleval);
                lily_impl_puts(data, fmtbuf);
            }

            str_start = fmt + i + 1;
            arg_pos++;
        }
        i++;
    }

    lily_impl_puts(data, str_start);
}
/** VM opcode helpers **/

/* op_ref_assign
   VM helper called for handling complex assigns. [1] is lhs, [2] is rhs. This
   does an assign along with the appropriate ref/deref stuff. This is suitable
   for anything that needs that ref/deref stuff except for any. */
void op_ref_assign(lily_value *lhs_reg, lily_value *rhs_reg)
{
    if ((lhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
        lily_deref_unknown_val(lhs_reg);

    if ((rhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
        rhs_reg->value.generic->refcount++;

    lhs_reg->flags = rhs_reg->flags;
    lhs_reg->value = rhs_reg->value;
}

/*  op_any_assign
    This is a vm helper for handling an assignment to an any from another value
    that may or may not be an any.
    Since this call only uses two values, those are passed instead of using
    vm_regs and code like some other vm helpers do.
    vm:      If lhs_reg is nil, an any will be made that needs a gc entry.
             The entry will be added to the vm's gc entries.
    lhs_reg: The register containing an any to be assigned to. Might be nil.
    rhs_reg: The register providing a value for the any. Might be nil. */
static void op_any_assign(lily_vm_state *vm, lily_value *lhs_reg,
        lily_value *rhs_reg)
{
    lily_any_val *lhs_any;
    if (lhs_reg->flags & VAL_IS_NIL) {
        lhs_any = lily_try_new_any_val();
        if (lhs_any == NULL ||
            lily_try_add_gc_item(vm, lhs_reg->sig,
                    (lily_generic_gc_val *)lhs_any) == 0) {

            if (lhs_any)
                lily_free(lhs_any->inner_value);

            lily_free(lhs_any);
            lily_raise_nomem(vm->raiser);
        }

        lhs_reg->value.any = lhs_any;
        lhs_reg->flags &= ~VAL_IS_NIL;
    }
    else
        lhs_any = lhs_reg->value.any;

    lily_sig *new_sig;
    lily_raw_value new_value;
    int new_flags;

    if (rhs_reg->sig->cls->id == SYM_CLASS_ANY) {
        if ((rhs_reg->flags & VAL_IS_NIL) ||
            (rhs_reg->value.any->inner_value->flags & VAL_IS_NIL)) {

            new_sig = NULL;
            new_value.integer = 0;
            new_flags = VAL_IS_NIL;
        }
        else {
            lily_value *rhs_inner = rhs_reg->value.any->inner_value;

            new_sig = rhs_inner->sig;
            new_value = rhs_inner->value;
            new_flags = rhs_inner->flags;
        }
    }
    else {
        new_sig = rhs_reg->sig;
        new_value = rhs_reg->value;
        new_flags = rhs_reg->flags;
    }

    if ((new_flags & VAL_IS_NIL_OR_PROTECTED) == 0 &&
        new_sig->cls->is_refcounted)
        new_value.generic->refcount++;

    lily_value *lhs_inner = lhs_any->inner_value;
    if ((lhs_inner->flags & VAL_IS_NIL_OR_PROTECTED) == 0 &&
        lhs_inner->sig->cls->is_refcounted)
        lily_deref_unknown_val(lhs_inner);

    lhs_inner->sig = new_sig;
    lhs_inner->value = new_value;
    lhs_inner->flags = new_flags;
}

/*  update_hash_key_value
    This attempts to set a new value for a given hash key. This first checks
    for an existing key to set. If none is found, then it attempts to create a
    new entry in the given hash with the given key and value.

    vm:          The vm that the hash is in. This is passed in case ErrNoMem
                 needs to be raised.
    hash:        A valid hash, which may or may not have elements.
    key_siphash: The calculated siphash of the given key. Use
                 lily_calculate_siphash to get this.
    hash_key:    The key value, used for lookup. This should not be nil.
    hash_value:  The new value to associate with the given key. This may or may
                 not be nil.

    This raises ErrNoMem on failure. */
static void update_hash_key_value(lily_vm_state *vm, lily_hash_val *hash,
        uint64_t key_siphash, lily_value *hash_key,
        lily_value *hash_value)
{
    lily_hash_elem *elem;
    elem = lily_try_lookup_hash_elem(hash, key_siphash, hash_key);

    if (elem == NULL) {
        elem = lily_try_new_hash_elem();
        if (elem != NULL) {
            /* It's important to copy over the flags, in case the key is a
               literal and marked VAL_IS_PROTECTED. Doing so keeps hash deref
               from trying to deref the key. */
            elem->elem_key->flags = hash_key->flags;
            elem->elem_key->value = hash_key->value;
            elem->elem_key->sig = hash_key->sig;
            elem->key_siphash = key_siphash;

            /* lily_assign_value needs a sig for the left side. */
            elem->elem_value->sig = hash_value->sig;

            elem->next = hash->elem_chain;
            hash->elem_chain = elem;

            hash->num_elems++;
        }
    }

    if (elem != NULL)
        lily_assign_value(vm, elem->elem_value, hash_value);
    else
        lily_raise_nomem(vm->raiser);
}

/*  op_set_item
    This handles subscript assignment for both lists and hashes.
    The vm calls this with the code and the current code position. There are
    three arguments to unpack:
    +2 is the list or hash to assign to. If the hash is nil, a hash value is
       created and a value is put inside.
    +3 is the index. This must not be nil. Lists only allow integer indexes,
       while this is a hash key of the correct type.
    +4 is the new value. This allowed to be nil. */
static void op_set_item(lily_vm_state *vm, uintptr_t *code, int code_pos)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *lhs_reg, *index_reg, *rhs_reg;

    lhs_reg = vm_regs[code[code_pos + 2]];
    LOAD_CHECKED_REG(index_reg, code_pos, 3)
    rhs_reg = vm_regs[code[code_pos + 4]];

    if (lhs_reg->sig->cls->id == SYM_CLASS_LIST) {
        lily_list_val *list_val = lhs_reg->value.list;
        int index_int = index_reg->value.integer;

        LOAD_CHECKED_REG(lhs_reg, code_pos, 2)

        if (index_int >= list_val->num_values)
            boundary_error(vm, code_pos, index_int);

        /* todo: Wraparound would be nice. */
        if (index_int < 0)
            boundary_error(vm, code_pos, index_int);

        lily_assign_value(vm, list_val->elems[index_int], rhs_reg);
    }
    else {
        if (lhs_reg->flags & VAL_IS_NIL) {
            lily_hash_val *hv = lily_try_new_hash_val();
            if (hv == NULL)
                lily_raise_nomem(vm->raiser);

            lhs_reg->value.hash = hv;
            lhs_reg->flags &= ~VAL_IS_NIL;
        }
        uint64_t siphash;
        siphash = lily_calculate_siphash(vm->sipkey, index_reg);

        update_hash_key_value(vm, lhs_reg->value.hash, siphash, index_reg,
                rhs_reg);
    }
}

/*  op_get_item
    This handles subscripting an element from the given hash or list.
    The vm calls this with the code and the current code position. There are
    three arguments to unpack:
    +2 is the list or hash to take a value from.
    +3 is the index to lookup. For hashes, ErrNoSuchKey if the key is not in
       the hash.
    +4 is the new value. This allowed to be nil.

    Note: If the hash is nil, then ErrNoValue is raised instead of
          ErrNoSuchKey. */
static void op_get_item(lily_vm_state *vm, uintptr_t *code, int code_pos)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *lhs_reg, *index_reg, *result_reg;

    /* The lhs is checked, unlike with o_set_item. The reason for this is not
       finding a key results in ErrNoSuchKey. So creating a hash where none
       exists would be useless, because the key that this wants is not going to
       be in an empty hash. */
    LOAD_CHECKED_REG(lhs_reg, code_pos, 2)
    LOAD_CHECKED_REG(index_reg, code_pos, 3)
    result_reg = vm_regs[code[code_pos + 4]];

    /* list and tuple have the same representation internally. Since list
       stores proper values, lily_assign_value automagically set the type to
       the right thing. */
    if (lhs_reg->sig->cls->id == SYM_CLASS_LIST ||
        lhs_reg->sig->cls->id == SYM_CLASS_TUPLE) {
        lily_list_val *list_val = lhs_reg->value.list;
        int index_int = index_reg->value.integer;

        /* Too big! */
        if (index_int >= lhs_reg->value.list->num_values)
            boundary_error(vm, code_pos, index_int);

        /* todo: Wraparound would be nice. */
        if (index_int < 0)
            boundary_error(vm, code_pos, index_int);

        lily_assign_value(vm, result_reg, list_val->elems[index_int]);
    }
    else {
        uint64_t siphash;
        lily_hash_elem *hash_elem;

        siphash = lily_calculate_siphash(vm->sipkey, index_reg);
        hash_elem = lily_try_lookup_hash_elem(lhs_reg->value.hash,
                siphash, index_reg);

        /* Give up if the key doesn't exist. */
        if (hash_elem == NULL)
            no_such_key_error(vm, code_pos, index_reg);

        lily_assign_value(vm, result_reg, hash_elem->elem_value);
    }
}

void op_build_hash(lily_vm_state *vm, uintptr_t *code, int code_pos)
{
    lily_value **vm_regs = vm->vm_regs;
    int i, num_values;
    lily_value *result, *key_reg, *value_reg;

    num_values = (intptr_t)(code[code_pos + 2]);
    result = vm_regs[code[code_pos + 3 + num_values]];

    lily_hash_val *hash_val = lily_try_new_hash_val();
    if (hash_val == NULL)
        lily_raise_nomem(vm->raiser);

    if ((result->sig->flags & SIG_MAYBE_CIRCULAR) &&
        lily_try_add_gc_item(vm, result->sig,
            (lily_generic_gc_val *)hash_val) == 0) {
        lily_free(hash_val);
        lily_raise_nomem(vm->raiser);
    }

    if ((result->flags & VAL_IS_NIL) == 0)
        lily_deref_hash_val(result->sig, result->value.hash);

    result->value.hash = hash_val;
    result->flags = 0;

    for (i = 0;
         i < num_values;
         i += 2) {
        key_reg = vm_regs[code[code_pos + 3 + i]];
        value_reg = vm_regs[code[code_pos + 3 + i + 1]];

        if (key_reg->flags & VAL_IS_NIL)
            lily_raise_nomem(vm->raiser);

        uint64_t key_siphash;
        key_siphash = lily_calculate_siphash(vm->sipkey, key_reg);

        update_hash_key_value(vm, hash_val, key_siphash, key_reg, value_reg);
    }
}

/* op_build_list
   VM helper called for handling o_build_list. This is a bit tricky, becaus the
   storage may have already had a previous list assigned to it. Additionally,
   the new list info may fail to allocate. If it does, ErrNoMem is raised. */
void op_build_list(lily_vm_state *vm, lily_value **vm_regs,
        uintptr_t *code)
{
    int num_elems = (intptr_t)(code[2]);
    int j;
    lily_value *result = vm_regs[code[3+num_elems]];
    lily_sig *elem_sig = result->sig->siglist[0];

    lily_list_val *lv = lily_malloc(sizeof(lily_list_val));
    if (lv == NULL)
        lily_raise_nomem(vm->raiser);

    /* This is set in case the gc looks at this list. This prevents the gc and
       deref calls from touching ->values and ->flags. */
    lv->num_values = -1;
    lv->visited = 0;
    lv->refcount = 1;
    lv->elems = lily_malloc(num_elems * sizeof(lily_value *));
    lv->gc_entry = NULL;

    /* If attaching a gc entry fails, then list deref will collect everything
       which is what's wanted anyway. */
    if (lv->elems == NULL ||
        ((result->sig->flags & SIG_MAYBE_CIRCULAR) &&
          lily_try_add_gc_item(vm, result->sig,
                (lily_generic_gc_val *)lv) == 0)) {

        lily_deref_list_val(result->sig, lv);
        lily_raise_nomem(vm->raiser);
    }

    /* The old value can be destroyed, now that the new value has been made. */
    if ((result->flags & VAL_IS_NIL) == 0)
        lily_deref_list_val(result->sig, result->value.list);

    /* Put the new list in the register so the gc doesn't try to collect it. */
    result->value.list = lv;
    /* This is important for list[any], because it prevents the gc from
       collecting all anys if it's triggered from within the list build. */
    result->flags = 0;

    /* List deref expects that num_elems elements are all allocated.
       Unfortunately, this means having to allocate during each loop. */
    if (elem_sig->cls->id == SYM_CLASS_ANY) {
        for (j = 0;j < num_elems;j++) {
            lv->elems[j] = lily_malloc(sizeof(lily_value));
            if (lv->elems[j] == NULL) {
                lv->num_values = j;
                lily_raise_nomem(vm->raiser);
            }

            /* Fix the element in case the next attempt to grab an any
               triggers the gc. */
            lily_value *new_elem = lv->elems[j];
            new_elem->sig = elem_sig;
            new_elem->flags = VAL_IS_NIL;
            new_elem->value.integer = 0;
            lv->num_values = j + 1;
            lily_value *rhs_reg = vm_regs[code[3+j]];

            if ((rhs_reg->flags & VAL_IS_NIL) == 0) {
                lily_value *rhs_inner_val;
                rhs_inner_val = rhs_reg->value.any->inner_value;
                /* Anys are supposed to act like containers which can hold
                   any value. Because of this, the inner value of the other
                   any must be copied over.
                   Anys are also potentially circular, which means each one
                   needs a gc entry. This also means that the list holding the
                   anys has a gc entry. */
                lily_any_val *oval = lily_try_new_any_val();
                if (oval == NULL ||
                    lily_try_add_gc_item(vm, elem_sig,
                            (lily_generic_gc_val *)oval) == 0) {
                    /* Make sure to free the any value made. If it wasn't
                       made, this will be NULL, which is fine. */
                    if (oval)
                        lily_free(oval->inner_value);

                    lily_free(oval);

                    /* Give up. The gc will have an entry for the list, so it
                       will correctly collect the list. */
                    lily_raise_nomem(vm->raiser);
                }

                oval->inner_value->value = rhs_inner_val->value;
                oval->inner_value->sig = rhs_inner_val->sig;
                oval->inner_value->flags = rhs_inner_val->flags;
                oval->refcount = 1;

                if ((rhs_inner_val->flags & VAL_IS_NIL_OR_PROTECTED) == 0 &&
                    rhs_inner_val->sig->cls->is_refcounted)
                    rhs_inner_val->value.generic->refcount++;

                new_elem->value.any = oval;
                new_elem->flags = 0;
            }

        }
    }
    else {
        int is_refcounted = elem_sig->cls->is_refcounted;
        for (j = 0;j < num_elems;j++) {
            lv->elems[j] = lily_malloc(sizeof(lily_value));
            if (lv->elems[j] == NULL) {
                lv->num_values = j;
                /* The gc will come later and collect this list. */
                lily_raise_nomem(vm->raiser);
            }

            lily_value *elem = lv->elems[j];
            elem->sig = elem_sig;
            elem->flags = VAL_IS_NIL;
            elem->value.integer = 0;
            lv->num_values = j + 1;

            lily_value *rhs_reg = vm_regs[code[3+j]];
            if ((rhs_reg->flags & VAL_IS_NIL) == 0) {
                lv->elems[j]->value = rhs_reg->value;
                if (is_refcounted && (rhs_reg->flags & VAL_IS_PROTECTED) == 0)
                    rhs_reg->value.generic->refcount++;
            }

            lv->elems[j]->flags = rhs_reg->flags;
        }
    }

    lv->num_values = num_elems;
}

void op_build_tuple(lily_vm_state *vm, lily_value **vm_regs,
        uintptr_t *code)
{
    int num_elems = (intptr_t)(code[2]);
    lily_value *result = vm_regs[code[3+num_elems]];
    lily_list_val *tuple = lily_malloc(sizeof(lily_list_val));
    if (tuple == NULL)
        lily_raise_nomem(vm->raiser);

    /* This is set in case the gc looks at this list. This prevents the gc and
       deref calls from touching ->values and ->flags. */
    tuple->num_values = -1;
    tuple->visited = 0;
    tuple->refcount = 1;
    tuple->elems = lily_malloc(num_elems * sizeof(lily_value *));
    tuple->gc_entry = NULL;

    /* If attaching a gc entry fails, then list deref will collect everything
       which is what's wanted anyway. */
    if (tuple->elems == NULL ||
        ((result->sig->flags & SIG_MAYBE_CIRCULAR) &&
          lily_try_add_gc_item(vm, result->sig,
                (lily_generic_gc_val *)tuple) == 0)) {

        lily_deref_tuple_val(result->sig, tuple);
        lily_raise_nomem(vm->raiser);
    }

    /* Deref the old value and install the new one. */
    if ((result->flags & VAL_IS_NIL) == 0)
        lily_deref_tuple_val(result->sig, result->value.list);

    result->value.list = tuple;
    /* This must be done in case the old tuple was nil. */
    result->flags = 0;

    int i;
    for (i = 0;i < num_elems;i++) {
        tuple->elems[i] = lily_malloc(sizeof(lily_value));
        if (tuple->elems[i] == NULL) {
            tuple->num_values = i;
            lily_raise_nomem(vm->raiser);
        }
        tuple->elems[i]->flags = VAL_IS_NIL;
        tuple->elems[i]->sig = result->sig->siglist[i];
        tuple->elems[i]->value.integer = 0;
        tuple->num_values = i + 1;
        lily_value *rhs_reg = vm_regs[code[3+i]];

        lily_assign_value(vm, tuple->elems[i], rhs_reg);
    }

    tuple->num_values = num_elems;
}

static void do_keyword_show(lily_vm_state *vm, int is_global, int reg_id)
{
    lily_value *reg;
    lily_function_val *lily_main;
    lily_function_val *current_function;

    if (is_global)
        reg = vm->regs_from_main[reg_id];
    else
        reg = vm->vm_regs[reg_id];

    lily_main = vm->function_stack[0]->function;
    current_function = vm->function_stack[vm->function_stack_pos - 1]->function;
    lily_show_sym(lily_main, current_function, reg, is_global, reg_id,
            vm->raiser->msgbuf, vm->data);
}

/** vm registers handling and stack growing **/

/* grow_vm_registers
   Increase the amount of registers available to the given 'register_need'.  */
static void grow_vm_registers(lily_vm_state *vm, int register_need)
{
    lily_value **new_regs;
    lily_sig *integer_sig = vm->integer_sig;
    int i = vm->max_registers;

    ptrdiff_t reg_offset = vm->vm_regs - vm->regs_from_main;

    /* Remember, use regs_from_main, NOT vm_regs, which is likely adjusted. */
    new_regs = lily_realloc(vm->regs_from_main, register_need *
            sizeof(lily_value));

    if (new_regs == NULL)
        lily_raise_nomem(vm->raiser);

    /* Realloc can move the pointer, so always recalculate vm_regs again using
       regs_from_main and the offset. */
    vm->regs_from_main = new_regs;
    vm->vm_regs = new_regs + reg_offset;

    /* Start creating new registers. Have them default to an integer sig so that
       nothing has to check for a NULL sig. Integer is used as the default
       because it is not ref'd. */
    for (;i < register_need;i++) {
        new_regs[i] = lily_malloc(sizeof(lily_value));
        if (new_regs[i] == NULL) {
            vm->max_registers = i;
            lily_raise_nomem(vm->raiser);
        }

        new_regs[i]->sig = integer_sig;
        new_regs[i]->flags = VAL_IS_NIL;
    }

    vm->max_registers = register_need;
}

/* prep_registers
   This sets up the registers for a function call. This is called after
   grow_registers, and thus assumes that the right number of registers are
   available.
   This also handles copying args for functions. */
static void prep_registers(lily_vm_state *vm, lily_function_val *fval,
        uintptr_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value **regs_from_main = vm->regs_from_main;
    lily_register_info *register_seeds = fval->reg_info;
    int num_registers = vm->num_registers;
    int register_need = vm->num_registers + fval->reg_count;
    int i;

    /* A function's args always come first, so copy arguments over while clearing
       old values. */
    for (i = 0;i < code[4];i++, num_registers++) {
        lily_register_info seed = register_seeds[i];
        lily_value *get_reg = vm_regs[code[5+i]];
        lily_value *set_reg = regs_from_main[num_registers];

        /* The get must be run before the set. Otherwise, if
           something has 1 ref and assigns to itself, it will be
           destroyed from a deref, then an invalid value ref'd.
           This may not be possible here, but it is elsewhere. */
        if (get_reg->sig->cls->is_refcounted &&
            ((get_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0))
            get_reg->value.generic->refcount++;

        if (set_reg->sig->cls->is_refcounted &&
            ((set_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0))
            lily_deref_unknown_val(set_reg);

        set_reg->sig = seed.sig;
        /* This will be null if this register doesn't belong to a
           var, or non-null if it's for a local. */

        if ((get_reg->flags & VAL_IS_NIL) == 0)
            set_reg->value = get_reg->value;
        else
            set_reg->value.integer = 0;

        set_reg->flags = get_reg->flags;
    }

    /* For the rest of the registers, clear whatever value they have. */
    for (;num_registers < register_need;i++, num_registers++) {
        lily_register_info seed = fval->reg_info[i];

        lily_value *reg = regs_from_main[num_registers];
        if (reg->sig->cls->is_refcounted &&
            (reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
            lily_deref_unknown_val(reg);

        /* SET the flags to nil so that VAL_IS_PROTECTED gets blasted away if
           it happens to be set. */
        reg->flags = VAL_IS_NIL;
        reg->sig = seed.sig;
    }

    vm->num_registers = num_registers;
}

/** Foreign calls **/

/*  lily_vm_foreign_call
    This calls the vm from a foreign source. The stack is adjusted before and
    after this call, so it can be done as many times as needed.

    Caveats:
    * lily_vm_foreign_prep MUST be called before this. It sets up the stack
      and the registers for the callee function. The function passed to
      lily_vm_foreign_prep is the one that is called. If it isn't called, the
      vm will crash.

    * lily_vm_foreign_load_by_val needs to be called to give values to the
      registers that the callee uses. If it isn't called, the registers will
      simply be nil. */
void lily_vm_foreign_call(lily_vm_state *vm)
{
    /* Simulate the stack growing before entry. This is reserved until now so
       that any stack trace does not show up as being inside of the function.
       -1: This is the native function that called the foreign function.
       +0: This is the foreign function that was called. */
    int regs_adjust = vm->function_stack[vm->function_stack_pos-1]->regs_used +
                      vm->function_stack[vm->function_stack_pos]->regs_used;

    /* Make it so the callee's register indexes target the right things. */
    vm->vm_regs += regs_adjust;

    /* The foreign entry added itself to the stack properly, so just add one
       for the native entry. */
    vm->function_stack_pos++;


    lily_vm_execute(vm);


    /* Now simulate a stack exit. */

    /* -1 is correct. The callee function will return via a normal way, and bring
       the function stack down by -1. This does a final adjustment. */
    vm->function_stack_pos--;
    vm->vm_regs -= vm->function_stack[vm->function_stack_pos-1]->regs_used;
}

/*  lily_vm_get_foreign_reg
    Obtain a value, adjusted for the function to be called. 0 is the value of the
    return (if there is one). Otherwise, this may not be useful. */
lily_value *lily_vm_get_foreign_reg(lily_vm_state *vm, int reg_pos)
{
    lily_value **vm_regs = vm->vm_regs;
    int load_start = vm->function_stack[vm->function_stack_pos-1]->regs_used +
                     vm->function_stack[vm->function_stack_pos]->regs_used;

    /* Intentionally adjust by -1 so the return register will be at 0. */
    return vm_regs[load_start - 1 + reg_pos];
}

/*  lily_vm_foreign_load_by_val
    This loads a value into the callee function at the given position. The
    callee function's registers start at 1.

    vm:        The vm to do the foreign call.
    index:     The index to use. Arguments start at index 1.
    new_value: The new value to give the register at the given index.

    Caveats:
    * Do not assign an invalid value to the given position. */
void lily_vm_foreign_load_by_val(lily_vm_state *vm, int index,
        lily_value *new_value)
{
    lily_value **vm_regs = vm->vm_regs;
    int load_start = vm->function_stack[vm->function_stack_pos-1]->regs_used +
                     vm->function_stack[vm->function_stack_pos]->regs_used;

    /* Intentionally adjust by -1 so the first arg starts at index 1. */
    lily_value *reg = vm_regs[load_start - 1 + index];
    lily_assign_value(vm, reg, new_value);
}

/*  lily_vm_foreign_prep
    Prepare the given vm for a call from a foreign function. This will ensure
    that the vm has stack space, and also initialize the registers to the
    proper types. The stack entry is updated, but the stack depth is not
    updated (so that raises show the proper value).

    vm:     The vm to prepare to enter.
    caller: The function that is doing the call. This function is added to the
            fake entry so that the stack trace prints out the function's
            information.
    tocall: A value holding a non-nil function to call. */
void lily_vm_foreign_prep(lily_vm_state *vm, lily_function_val *caller,
        lily_value *to_call)
{
    /* Warning: This assumes that the function isn't nil. This may not be true in
                the future. */

    /* Step 1: Determine the total register need of this function. */
    int register_need = to_call->value.function->reg_count;
    lily_sig *function_val_return_sig = to_call->sig->siglist[0];
    lily_function_val *function_val = to_call->value.function;
    /* In normal function calls, the caller is a function that reserves a
       register to get a value back from the callee. Since that is not the
       case here, add one more register to get a value in case one is
       needed. */
    if (function_val_return_sig != NULL)
        register_need++;

    register_need += vm->num_registers;

    /* Step 2: If there aren't enough registers, make them. This may fail. */
    if (register_need > vm->max_registers) {
        grow_vm_registers(vm, register_need);
        /* grow_vm_registers doesn't set this because prep_registers often
           comes after it and initializes registers from vm->num_registers to
           vm->max_registers. So...fix this up. */
        vm->num_registers = register_need;
    }

    /* Step 3: If there's a return register, set it to the proper signature.
               -2 may seem deep, but it's correct: 0 is the new native callee,
               -1 is foreign callee, and -2 is the native caller. The result
               register will be in the native caller's registers (so that the) */
    int function_vals_used = vm->function_stack[vm->function_stack_pos-2]->regs_used;

    if (function_val_return_sig != NULL) {
        lily_value *foreign_reg = vm->vm_regs[function_vals_used];
        /* Set it to the right sig, in case something looks at it later. */
        foreign_reg->sig = function_val_return_sig;
    }

    /* Step 4: Make sure there is enough room for the new native entry. The
               foreign function already had an entry added which just needs to
               be updated. */
    if (vm->function_stack_pos + 1 == vm->function_stack_size)
        grow_function_stack(vm);

    /* Step 5: Update the foreign function that was entered so that its code
       returns to 'o_return_from_vm' to get back into that function. */
    lily_vm_stack_entry *foreign_entry = vm->function_stack[vm->function_stack_pos-1];
    foreign_entry->code = vm->foreign_code;
    foreign_entry->code_pos = 0;
    foreign_entry->regs_used = (function_val_return_sig != NULL);

    /* Step 6: Set the second stack entry (the native function). */
    lily_vm_stack_entry *native_entry = vm->function_stack[vm->function_stack_pos];
    native_entry->code = function_val->code;
    native_entry->code_pos = 0;
    native_entry->regs_used = function_val->reg_count;
    native_entry->return_reg = 0;
    native_entry->function = function_val;
    native_entry->line_num = 0;
}

/** VM prep, API functions  **/

/* load_vm_regs
   This is a helper for lily_vm_prep that loads a chain of vars into the
   registers given. */
static void load_vm_regs(lily_value **vm_regs, lily_var *iter_var)
{
    while (iter_var) {
        /* These shouldn't be marked as protected, because they're vars and
           they haven't had a chance to be assigned to anything yet. */
        if ((iter_var->flags & (VAL_IS_NIL | VAR_IS_READONLY)) == 0) {
            if (iter_var->sig->cls->is_refcounted)
                iter_var->value.generic->refcount++;

            vm_regs[iter_var->reg_spot]->flags &= ~VAL_IS_NIL;
            vm_regs[iter_var->reg_spot]->value = iter_var->value;
        }

        iter_var = iter_var->next;
    }
}

/* lily_vm_prep
   This is called before lily_vm_execute to make sure that all global values are
   copied into starting registers. */
void lily_vm_prep(lily_vm_state *vm, lily_symtab *symtab)
{
    lily_var *main_var = symtab->var_start;
    lily_function_val *main_function = main_var->value.function;
    int i;
    lily_var *prep_var_start = vm->prep_var_start;
    if (prep_var_start == NULL)
        prep_var_start = main_var;

    lily_value **vm_regs;
    if (vm->num_registers > main_function->reg_count)
        vm_regs = vm->vm_regs;
    else {
        /* Note: num_registers can never be zero for a second pass, because the
           first pass will have at least __main__ and the sys package even if
           there's no code to run. */
        if (vm->num_registers == 0)
            vm_regs = lily_malloc(main_function->reg_count *
                    sizeof(lily_value *));
        else
            vm_regs = lily_realloc(vm->regs_from_main,
                    main_function->reg_count * sizeof(lily_value *));

        if (vm_regs == NULL)
            lily_raise_nomem(vm->raiser);

        vm->vm_regs = vm_regs;
    }

    vm->regs_from_main = vm_regs;

    /* Do a special pass to make sure there are values. This allows the second
       loop to just worry about initializing the registers. */
    if (main_function->reg_count > vm->num_registers) {
        for (i = vm->max_registers;i < main_function->reg_count;i++) {
            lily_value *reg = lily_malloc(sizeof(lily_value));
            vm_regs[i] = reg;
            if (reg == NULL) {
                for (;i >= vm->max_registers;i--)
                    lily_free(vm_regs[i]);

                lily_raise_nomem(vm->raiser);
            }
        }
    }

    for (i = vm->prep_id_start;i < main_function->reg_count;i++) {
        lily_value *reg = vm_regs[i];
        lily_register_info seed = main_function->reg_info[i];

        /* This allows opcodes to copy over a register value without checking
           if VAL_IS_NIL is set. This is fine, because the value won't actually
           be used if VAL_IS_NIL is set (optimization!) */
        reg->value.integer = 0;
        reg->flags = VAL_IS_NIL;
        reg->sig = seed.sig;
    }

    load_vm_regs(vm_regs, prep_var_start);

    /* Load only the new globals next time. */
    vm->prep_var_start = symtab->var_top;
    /* Zap only the slots that new globals need next time. */
    vm->prep_id_start = i;

    if (main_function->reg_count > vm->num_registers) {
        if (vm->num_registers == 0) {
            lily_class *integer_cls = lily_class_by_id(symtab, SYM_CLASS_INTEGER);
            vm->integer_sig = integer_cls->sig;
            vm->main = main_var;
        }

        vm->num_registers = main_function->reg_count;
        vm->max_registers = main_function->reg_count;
    }

    lily_vm_stack_entry *stack_entry = vm->function_stack[0];
    stack_entry->function = main_function;
    stack_entry->code = main_function->code;
    stack_entry->regs_used = main_function->reg_count;
    stack_entry->return_reg = 0;
    stack_entry->code_pos = 0;
    vm->function_stack_pos = 1;
}

/*  lily_try_lookup_hash_elem
    This attempts to find a hash element by key in the given hash. This will not
    create a new element if it fails.

    hash:        A valid hash, which may or may not have elements.
    key_siphash: The calculated siphash of the given key. Use
                 lily_calculate_siphash to get this.
    key:         The key used for doing the search.

    On success: The hash element that was inserted into the hash value is
                returned.
    On failure: NULL is returned. */
lily_hash_elem *lily_try_lookup_hash_elem(lily_hash_val *hash,
        uint64_t key_siphash, lily_value *key)
{
    int key_cls_id = key->sig->cls->id;

    lily_hash_elem *elem_iter = hash->elem_chain;
    lily_raw_value key_value = key->value;

    while (elem_iter) {
        if (elem_iter->key_siphash == key_siphash) {
            int ok;
            lily_raw_value iter_value = elem_iter->elem_key->value;

            if (key_cls_id == SYM_CLASS_INTEGER &&
                iter_value.integer == key_value.integer)
                ok = 1;
            else if (key_cls_id == SYM_CLASS_DOUBLE &&
                     iter_value.doubleval == key_value.doubleval)
                ok = 1;
            else if (key_cls_id == SYM_CLASS_STRING &&
                    /* strings are immutable, so try a ptr compare first. */
                    ((iter_value.string == key_value.string) ||
                     /* No? Make sure the sizes match, then call for a strcmp.
                        The size check is an easy way to potentially skip a
                        strcmp in case of hash collision. */
                      (iter_value.string->size == key_value.string->size &&
                       strcmp(iter_value.string->string,
                              key_value.string->string) == 0)))
                ok = 1;
            else
                ok = 0;

            if (ok)
                break;
        }
        elem_iter = elem_iter->next;
    }

    return elem_iter;
}

/*  lily_assign_value
    This is an extremely handy function that assigns 'left' to 'right'. This is
    handy because it will handle any refs/derefs needed, nil, and any
    copying.

    vm:    The vm holding the two values. This is needed because if 'left' is
           an any, then a gc pass may be triggered.
    left:  The value to assign to.
           The type of left determines what assignment is used. This is
           important because it means that left must have a type set.
    right: The value to assign.

    Caveats:
    * May raise a nomem error if left is an any and it cannot allocate a
      value to copy right's value.
    * May trigger the vm if if needs to make a new any.
    * Will crash if left does not have a type set. */
void lily_assign_value(lily_vm_state *vm, lily_value *left, lily_value *right)
{
    lily_class *cls = left->sig->cls;

    if (cls->id == SYM_CLASS_ANY)
        /* Any assignment is...complicated. Have someone else do it. */
        op_any_assign(vm, left, right);
    else {
        if (cls->is_refcounted) {
            if ((right->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                right->value.generic->refcount++;

            if ((left->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                lily_deref_unknown_val(left);
        }

        left->value = right->value;
        left->flags = right->flags;
    }
}

/*  lily_calculate_siphash
    Return a siphash based using the given siphash for the given key.

    sipkey:  The vm's sipkey for creating the hash.
    key:     A non-nil value to make a hash for.

    Notes:
    * The caller must not pass a non-hashable type (such as any). Parser is
      responsible for ensuring that hashes only use valid key types.
    * The caller must not pass a key that is a nil value. */
uint64_t lily_calculate_siphash(char *sipkey, lily_value *key)
{
    int key_cls_id = key->sig->cls->id;
    uint64_t key_hash;

    if (key_cls_id == SYM_CLASS_STRING)
        key_hash = siphash24(key->value.string->string,
                key->value.string->size, sipkey);
    else if (key_cls_id == SYM_CLASS_INTEGER)
        key_hash = key->value.integer;
    else if (key_cls_id == SYM_CLASS_DOUBLE)
        /* siphash thinks it's sent a pointer (and will try to deref it), so
           send the address. */
        key_hash = siphash24(&(key->value.doubleval), sizeof(double), sipkey);
    else /* Should not happen, because no other classes are valid keys. */
        key_hash = 0;

    return key_hash;
}

/** The mighty VM **/

/* lily_vm_execute
   This is the VM part of lily. It executes any code on __main__, as well as
   anything called by __main__. Finishes when it encounters the o_vm_return
   opcode.
   This function occasionally farms work out to other routines to keep the size
   from being too big. It does not recurse, instead saving everything necessary
   to the vm state for each call. */
void lily_vm_execute(lily_vm_state *vm)
{
    lily_function_val *f;
    uintptr_t *code;
    lily_vm_stack_entry *stack_entry;
    lily_value **regs_from_main;
    lily_value **vm_regs;
    int i, num_registers, max_registers;
    lily_sig *cast_sig;
    lily_var *temp_var;
    register int64_t for_temp;
    register int code_pos;
    register lily_value *lhs_reg, *rhs_reg, *loop_reg, *step_reg;
    register lily_sym *readonly_sym;
    lily_function_val *fval;

    f = vm->function_stack[vm->function_stack_pos-1]->function;
    code = f->code;

    /* Initialize local vars from the vm state's vars. */
    vm_regs = vm->vm_regs;
    regs_from_main = vm->regs_from_main;
    num_registers = vm->num_registers;
    max_registers = vm->max_registers;
    code_pos = 0;

    /* Only install a raiser jump on the first vm entry. */
    if (vm->function_stack_pos == 1) {
        if (setjmp(vm->raiser->jumps[vm->raiser->jump_pos]) == 0)
            vm->raiser->jump_pos++;
        else {
            lily_vm_stack_entry *top = vm->function_stack[vm->function_stack_pos-1];
            /* If the current function is a native one, then fix the line
               number of it. Otherwise, leave the line number alone. */
            if (top->function->code != NULL)
                top->line_num = top->code[code_pos+1];

            /* The top of the stack is always changing, so make sure the top's
               line number is set. This is safe because any opcode that can
               raise will also have a line number right after the opcode. */
            longjmp(vm->raiser->jumps[vm->raiser->jump_pos-2], 1);
        }
    }

    while (1) {
        switch(code[code_pos]) {
            case o_assign:
                rhs_reg = vm_regs[code[code_pos+2]];
                lhs_reg = vm_regs[code[code_pos+3]];
                lhs_reg->flags = rhs_reg->flags;
                lhs_reg->value = rhs_reg->value;
                code_pos += 4;
                break;
            case o_get_const:
                readonly_sym = (lily_sym *)code[code_pos+2];
                lhs_reg = vm_regs[code[code_pos+3]];

                if (lhs_reg->sig->cls->is_refcounted &&
                    (lhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                    lily_deref_unknown_val(lhs_reg);

                lhs_reg->value = readonly_sym->value;
                lhs_reg->flags = VAL_IS_PROTECTED;
                code_pos += 4;
                break;
            case o_integer_add:
                INTEGER_OP(+)
                break;
            case o_integer_minus:
                INTEGER_OP(-)
                break;
            case o_double_add:
                INTDBL_OP(+)
                break;
            case o_double_minus:
                INTDBL_OP(-)
                break;
            case o_less:
                COMPARE_OP(<, == -1)
                break;
            case o_less_eq:
                COMPARE_OP(<=, <= 0)
                break;
            case o_is_equal:
                EQUALITY_COMPARE_OP(==, == 0)
                break;
            case o_greater:
                COMPARE_OP(>, == 1)
                break;
            case o_greater_eq:
                COMPARE_OP(>, >= 0)
                break;
            case o_not_eq:
                EQUALITY_COMPARE_OP(!=, != 0)
                break;
            case o_jump:
                code_pos = code[code_pos+1];
                break;
            case o_modulo:
                INTEGER_OP(%)
                break;
            case o_integer_mul:
                INTEGER_OP(*)
                break;
            case o_double_mul:
                INTDBL_OP(*)
                break;
            case o_integer_div:
                /* Before doing INTEGER_OP, check for a division by zero. This
                   will involve some redundant checking of the rhs, but better
                   than dumping INTEGER_OP's contents here or rewriting
                   INTEGER_OP for the special case of division. */
                LOAD_CHECKED_REG(rhs_reg, code_pos, 3)
                if (rhs_reg->value.integer == 0)
                    divide_by_zero_error(vm, code_pos, 3);
                INTEGER_OP(/)
                break;
            case o_left_shift:
                INTEGER_OP(<<)
                break;
            case o_right_shift:
                INTEGER_OP(>>)
                break;
            case o_bitwise_and:
                INTEGER_OP(&)
                break;
            case o_bitwise_or:
                INTEGER_OP(|)
                break;
            case o_bitwise_xor:
                INTEGER_OP(^)
                break;
            case o_double_div:
                /* This is a little more tricky, because the rhs could be a
                   number or an integer... */
                LOAD_CHECKED_REG(rhs_reg, code_pos, 3)
                if (rhs_reg->sig->cls->id == SYM_CLASS_INTEGER &&
                    rhs_reg->value.integer == 0)
                    divide_by_zero_error(vm, code_pos, 3);
                else if (rhs_reg->sig->cls->id == SYM_CLASS_DOUBLE &&
                         rhs_reg->value.doubleval == 0)
                    divide_by_zero_error(vm, code_pos, 3);

                INTDBL_OP(/)
                break;
            case o_jump_if:
                lhs_reg = vm_regs[code[code_pos+2]];
                {
                    int cls_id, result;

                    if (lhs_reg->sig->cls->id == SYM_CLASS_ANY &&
                        (lhs_reg->flags & VAL_IS_NIL) == 0)
                        lhs_reg = lhs_reg->value.any->inner_value;

                    if ((lhs_reg->flags & VAL_IS_NIL) == 0) {
                        cls_id = lhs_reg->sig->cls->id;
                        if (cls_id == SYM_CLASS_INTEGER)
                            result = (lhs_reg->value.integer == 0);
                        else if (cls_id == SYM_CLASS_DOUBLE)
                            result = (lhs_reg->value.doubleval == 0);
                        else
                            result = 0;
                    }
                    else
                        result = 1;

                    if (result != code[code_pos+1])
                        code_pos = code[code_pos+3];
                    else
                        code_pos += 4;
                }
                break;
            case o_function_call:
            {
                if (vm->function_stack_pos+1 == vm->function_stack_size)
                    grow_function_stack(vm);

                if (code[code_pos+2] == 1)
                    fval = ((lily_var *)(code[code_pos+3]))->value.function;
                else {
                    lhs_reg = vm_regs[code[code_pos+3]];
                    fval = (lily_function_val *)(lhs_reg->value.function);
                }

                int j = code[code_pos+4];
                stack_entry = vm->function_stack[vm->function_stack_pos-1];
                stack_entry->line_num = code[code_pos+1];
                stack_entry->code_pos = code_pos + j + 6;

                if (fval->code != NULL) {
                    int register_need = fval->reg_count + num_registers;

                    if (register_need > max_registers) {
                        grow_vm_registers(vm, register_need);
                        /* Don't forget to update local info... */
                        regs_from_main = vm->regs_from_main;
                        vm_regs        = vm->vm_regs;
                        max_registers  = register_need;
                    }

                    /* Prepare the registers for what the function wants.
                       Afterward, update num_registers since prep_registers
                       changes it. */
                    prep_registers(vm, fval, code+code_pos);
                    num_registers = vm->num_registers;

                    vm_regs = vm_regs + stack_entry->regs_used;
                    vm->vm_regs = vm_regs;

                    stack_entry->return_reg =
                        -(stack_entry->function->reg_count - code[code_pos+5+j]);

                    stack_entry = vm->function_stack[vm->function_stack_pos];
                    stack_entry->function = fval;
                    stack_entry->regs_used = fval->reg_count;
                    stack_entry->code = fval->code;
                    vm->function_stack_pos++;

                    code = fval->code;
                    code_pos = 0;
                }
                else {
                    lily_foreign_func func = fval->foreign_func;
                    stack_entry = vm->function_stack[vm->function_stack_pos];
                    stack_entry->function = fval;
                    stack_entry->line_num = -1;
                    stack_entry->code = NULL;
                    vm->function_stack_pos++;

                    func(vm, fval, code+code_pos+5);
                    /* This function may have called the vm, thus growing the
                       number of registers. Copy over important data if that's
                       happened. */
                    if (vm->max_registers != max_registers) {
                        regs_from_main = vm->regs_from_main;
                        vm_regs        = vm->vm_regs;
                        max_registers  = vm->max_registers;
                    }
                    code_pos += 6 + j;
                    vm->function_stack_pos--;
                }
            }
                break;
            case o_unary_not:
                LOAD_CHECKED_REG(lhs_reg, code_pos, 2)

                rhs_reg = vm_regs[code[code_pos+3]];
                rhs_reg->flags &= ~VAL_IS_NIL;
                rhs_reg->value.integer = !(lhs_reg->value.integer);
                code_pos += 4;
                break;
            case o_unary_minus:
                LOAD_CHECKED_REG(lhs_reg, code_pos, 2)

                rhs_reg = vm_regs[code[code_pos+3]];
                rhs_reg->flags &= ~VAL_IS_NIL;
                rhs_reg->value.integer = -(lhs_reg->value.integer);
                code_pos += 4;
                break;
            case o_return_val:
                /* The current function is at -1.
                   This grabs for -2 because we need the register that the
                   caller reserved for the return. */

                stack_entry = vm->function_stack[vm->function_stack_pos-2];
                /* Note: Upon function entry, vm_regs is moved so that 0 is the
                   start of the new function's registers.
                   Because of this, the register return is a -negative- index
                   that goes back into the caller's stack. */

                lhs_reg = vm_regs[stack_entry->return_reg];
                rhs_reg = vm_regs[code[code_pos+2]];

                /* rhs_reg and lhs_reg are both the same type, so only one
                   is_refcounted check is necessary. */
                if (rhs_reg->sig->cls->is_refcounted) {
                    /* However, one or both could be nil. */
                    if ((rhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                        rhs_reg->value.generic->refcount++;

                    if ((lhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                        lily_deref_unknown_val(lhs_reg);
                }
                lhs_reg->flags = rhs_reg->flags;
                lhs_reg->value = rhs_reg->value;

                /* DO NOT BREAK HERE.
                   These two do the same thing from here on, so fall through to
                   share code. */
            case o_return_noval:
                vm->function_stack_pos--;
                stack_entry = vm->function_stack[vm->function_stack_pos-1];

                /* This is the function that was just left. These registers are no
                   longer used, so remove them from the total. */
                num_registers -= vm->function_stack[vm->function_stack_pos]->regs_used;
                vm->num_registers = num_registers;
                vm_regs = vm_regs - stack_entry->regs_used;
                vm->vm_regs = vm_regs;
                code = stack_entry->code;
                code_pos = stack_entry->code_pos;
                break;
            case o_get_global:
                rhs_reg = regs_from_main[code[code_pos+2]];
                lhs_reg = vm_regs[code[code_pos+3]];
                          /* Important: vm_regs starts at the local scope, and
                             this index is based on the global scope. */
                if (rhs_reg->sig->cls->id != SYM_CLASS_ANY) {
                    if (rhs_reg->sig->cls->is_refcounted) {
                        /* However, one or both could be nil. */
                        if ((rhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                            rhs_reg->value.generic->refcount++;

                        if ((lhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                            lily_deref_unknown_val(lhs_reg);
                    }
                    lhs_reg->flags = rhs_reg->flags;
                    lhs_reg->value = rhs_reg->value;
                }
                else
                    op_any_assign(vm, lhs_reg, rhs_reg);

                code_pos += 4;
                break;
            case o_set_global:
                rhs_reg = vm_regs[code[code_pos+2]];
                lhs_reg = regs_from_main[code[code_pos+3]];

                /* Use the lhs, because it may be a global any. */
                if (lhs_reg->sig->cls->id != SYM_CLASS_ANY) {
                    if (lhs_reg->sig->cls->is_refcounted) {
                        /* However, one or both could be nil. */
                        if ((rhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                            rhs_reg->value.generic->refcount++;

                        if ((lhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                            lily_deref_unknown_val(lhs_reg);
                    }

                    lhs_reg->flags = rhs_reg->flags;
                    lhs_reg->value = rhs_reg->value;
                }
                else
                    /* The lhs is an any, so do what any assign does. */
                    op_any_assign(vm, lhs_reg, rhs_reg);

                code_pos += 4;
                break;
            case o_return_expected:
            {
                lily_vm_stack_entry *top;
                top = vm->function_stack[vm->function_stack_pos-1];
                top->line_num = top->code[code_pos+1];
                lily_raise(vm->raiser, lily_ErrReturnExpected,
                        "Function %s completed without returning a value.\n",
                        top->function->trace_name);
            }
                break;
            case o_any_assign:
                rhs_reg = vm_regs[code[code_pos+2]];
                lhs_reg = vm_regs[code[code_pos+3]];

                op_any_assign(vm, lhs_reg, rhs_reg);
                code_pos += 4;
                break;
            case o_intdbl_typecast:
                lhs_reg = vm_regs[code[code_pos+3]];
                LOAD_CHECKED_REG(rhs_reg, code_pos, 2)

                /* Guaranteed to work, because rhs is non-nil and emitter has
                   already verified the types. This will also make sure that the
                   nil flag isn't set on lhs. */
                maybe_crossover_assign(lhs_reg, rhs_reg);
                code_pos += 4;
                break;
            case o_get_item:
                op_get_item(vm, code, code_pos);
                code_pos += 5;
                break;
            case o_set_item:
                op_set_item(vm, code, code_pos);
                code_pos += 5;
                break;
            case o_build_hash:
                op_build_hash(vm, code, code_pos);
                code_pos += code[code_pos+2] + 4;
                break;
            case o_build_list:
                op_build_list(vm, vm_regs, code+code_pos);
                code_pos += code[code_pos+2] + 4;
                break;
            case o_build_tuple:
                op_build_tuple(vm, vm_regs, code+code_pos);
                code_pos += code[code_pos+2] + 4;
                break;
            case o_ref_assign:
                lhs_reg = vm_regs[code[code_pos+3]];
                rhs_reg = vm_regs[code[code_pos+2]];

                op_ref_assign(lhs_reg, rhs_reg);
                code_pos += 4;
                break;
            case o_show:
                do_keyword_show(vm, code[code_pos+2], code[code_pos+3]);
                code_pos += 4;
                break;
            case o_any_typecast:
                lhs_reg = vm_regs[code[code_pos+3]];
                cast_sig = lhs_reg->sig;

                LOAD_CHECKED_REG(rhs_reg, code_pos, 2)
                if ((rhs_reg->flags & VAL_IS_NIL) ||
                    (rhs_reg->value.any->inner_value->flags & VAL_IS_NIL))
                    novalue_error(vm, code_pos, 2);

                rhs_reg = rhs_reg->value.any->inner_value;

                /* This works because lily_ensure_unique_sig makes sure that
                   no two signatures describe the same thing. So if it's the
                   same, then they share the same sig pointer. */
                if (cast_sig == rhs_reg->sig) {
                    if (lhs_reg->sig->cls->is_refcounted) {
                        if ((rhs_reg->flags & VAL_IS_PROTECTED) == 0)
                            rhs_reg->value.generic->refcount++;

                        if ((lhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                            lily_deref_unknown_val(lhs_reg);
                    }
                    lhs_reg->flags = rhs_reg->flags;
                    lhs_reg->value = rhs_reg->value;
                }
                /* Since integer and number can be cast between each other,
                   allow that with any casts as well. */
                else if (maybe_crossover_assign(lhs_reg, rhs_reg) == 0) {
                    lily_vm_stack_entry *top;
                    top = vm->function_stack[vm->function_stack_pos-1];
                    top->line_num = top->code[code_pos+1];

                    lily_raise(vm->raiser, lily_ErrBadCast,
                            "Cannot cast any containing type '%T' to type '%T'.\n",
                            rhs_reg->sig, lhs_reg->sig);
                }

                code_pos += 4;
                break;
            case o_integer_for:
                loop_reg = vm_regs[code[code_pos+2]];
                /* lhs is the start, and also incremented. This is done so that
                   user assignments cannot cause the loop to leave early. This
                   may be changed in the future.
                   rhs is the stop value. */
                lhs_reg  = vm_regs[code[code_pos+3]];
                rhs_reg  = vm_regs[code[code_pos+4]];
                step_reg = vm_regs[code[code_pos+5]];

                for_temp = lhs_reg->value.integer + step_reg->value.integer;
                /* Copied from Lua's for loop. */
                if ((step_reg->value.integer > 0)
                        /* Positive bound check */
                        ? (for_temp <= rhs_reg->value.integer)
                        /* Negative bound check */
                        : (for_temp >= rhs_reg->value.integer)) {

                    /* Haven't reached the end yet, so bump the internal and
                       external values.*/
                    lhs_reg->value.integer = for_temp;
                    loop_reg->value.integer = for_temp;
                    /* The loop var may have been altered and set nil. Make sure
                       it is not nil. */
                    loop_reg->flags &= ~VAL_IS_NIL;
                    code_pos += 7;
                }
                else
                    code_pos = code[code_pos+6];

                break;
            case o_package_set:
                lhs_reg = regs_from_main[code[code_pos + 2]];
                i = code[code_pos + 3];
                temp_var = lhs_reg->value.package->vars[i];
                rhs_reg = vm_regs[code[code_pos + 4]];
                /* Here, the temp receives the value. */
                lily_assign_value(vm, (lily_value *)temp_var, rhs_reg);

                code_pos += 5;
                break;
            case o_package_get:
                lhs_reg = regs_from_main[code[code_pos + 2]];
                i = code[code_pos + 3];
                temp_var = lhs_reg->value.package->vars[i];
                rhs_reg = vm_regs[code[code_pos + 4]];
                /* This time, the rhs takes the value. */
                lily_assign_value(vm, rhs_reg, (lily_value *)temp_var);

                code_pos += 5;
                break;
            case o_package_get_deep:
            {
                int loops;

                lhs_reg = regs_from_main[code[code_pos + 2]];
                /* This is a multi-level package access (ex: a::b::c). Each
                   package that isn't the last one will contain another package
                   to grab. So...keep doing that until the last one is hit. */
                for (loops = code[code_pos + 3];
                     loops > 0;
                     loops--, code_pos++) {
                    lhs_reg = (lily_value *)lhs_reg->value.package->vars
                            [code[code_pos + 4]];
                }

                rhs_reg = vm_regs[code[code_pos + 4]];

                lily_assign_value(vm, rhs_reg, (lily_value *)lhs_reg);
                code_pos += 5;
                break;
            }
            case o_package_set_deep:
            {
                /* This is exactly like o_package_set_deep except that the
                   assignment at the bottom is reversed. */
                int loops;

                lhs_reg = regs_from_main[code[code_pos + 2]];
                for (loops = code[code_pos + 3];
                     loops > 0;
                     loops--, code_pos++) {
                    lhs_reg = (lily_value *)lhs_reg->value.package->vars
                            [code[code_pos + 4]];
                }

                rhs_reg = vm_regs[code[code_pos + 4]];

                lily_assign_value(vm, (lily_value *)lhs_reg, rhs_reg);
                code_pos += 5;
                break;
            }
            case o_isnil:
            {
                int is_global = code[code_pos + 2];
                if (is_global)
                    rhs_reg = regs_from_main[code[code_pos + 3]];
                else
                    rhs_reg = vm_regs[code[code_pos + 3]];

                int is_nil;
                /* Consider anys nil if they are nil OR they carry a nil
                   value. Otherwise, a typecast to the last type given would
                   be needed to get the inner value to check that for nil-ness
                   too. */
                if (rhs_reg->sig->cls->id == SYM_CLASS_ANY) {
                    is_nil = (rhs_reg->flags & VAL_IS_NIL) ||
                             (rhs_reg->value.any->inner_value->flags & VAL_IS_NIL);
                }
                else
                    is_nil = (rhs_reg->flags & VAL_IS_NIL);

                lhs_reg = vm_regs[code[code_pos + 4]];
                lhs_reg->flags = 0;
                lhs_reg->value.integer = is_nil;
                code += 5;
                break;
            }
            case o_for_setup:
                loop_reg = vm_regs[code[code_pos+2]];
                /* lhs_reg is the start, rhs_reg is the stop. */
                step_reg = vm_regs[code[code_pos+5]];
                LOAD_CHECKED_REG(lhs_reg, code_pos, 3)
                LOAD_CHECKED_REG(rhs_reg, code_pos, 4)

                /* +6 is used to indicate if the step needs to be generated, or
                   if it's already calculated. */
                if (code[code_pos+6] == 1) {
                    if (lhs_reg->value.integer <= rhs_reg->value.integer)
                        step_reg->value.integer = +1;
                    else
                        step_reg->value.integer = -1;

                    step_reg->flags &= ~VAL_IS_NIL;
                }
                else if (step_reg->value.integer == 0) {
                    LOAD_CHECKED_REG(step_reg, code_pos, 5)

                    lily_raise(vm->raiser, lily_ErrBadValue,
                               "for loop step cannot be 0.\n");
                }

                loop_reg->value.integer = lhs_reg->value.integer;
                loop_reg->flags &= ~VAL_IS_NIL;

                code_pos += 7;
                break;
            case o_return_from_vm:
                /* Only the first entry to the vm adds a stack entry... */
                if (vm->function_stack_pos == 1)
                    vm->raiser->jump_pos--;
                return;
        }
    }
}
