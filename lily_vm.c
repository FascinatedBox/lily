#include <stddef.h>
#include <string.h>
#include <inttypes.h>

#include "lily_impl.h"
#include "lily_symtab.h"
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
if (load_reg->flags & SYM_IS_NIL) \
    novalue_error(vm, load_code_pos, load_pos); \

#define INTEGER_OP(OP) \
LOAD_CHECKED_REG(lhs_reg, code_pos, 2) \
LOAD_CHECKED_REG(rhs_reg, code_pos, 3) \
vm_regs[code[code_pos+4]]->value.integer = \
lhs_reg->value.integer OP rhs_reg->value.integer; \
vm_regs[code[code_pos+4]]->flags &= ~SYM_IS_NIL; \
code_pos += 5;

#define INTNUM_OP(OP) \
LOAD_CHECKED_REG(lhs_reg, code_pos, 2) \
LOAD_CHECKED_REG(rhs_reg, code_pos, 3) \
if (lhs_reg->sig->cls->id == SYM_CLASS_NUMBER) { \
    if (rhs_reg->sig->cls->id == SYM_CLASS_NUMBER) \
        vm_regs[code[code_pos+4]]->value.number = \
        lhs_reg->value.number OP rhs_reg->value.number; \
    else \
        vm_regs[code[code_pos+4]]->value.number = \
        lhs_reg->value.number OP rhs_reg->value.integer; \
} \
else \
    vm_regs[code[code_pos+4]]->value.number = \
    lhs_reg->value.integer OP rhs_reg->value.number; \
vm_regs[code[code_pos+4]]->flags &= ~SYM_IS_NIL; \
code_pos += 5;

/* EQUALITY_COMPARE_OP is used for == and !=, instead of a normal COMPARE_OP.
   The difference is that this will allow op on any type, so long as the lhs
   and rhs agree on the full type. This allows comparing methods, functions,
   lists, and more.

   Note: This results in lists being compared by pointer. As a result,
         [1] == [1] will return 0, because they are different lists.

   Arguments are:
   * op:    The operation to perform relative to the values given. This will be
            substituted like: lhs->value OP rhs->value
            This is done for everything BUT str.
   * strop: The operation to perform relative to the result of strcmp. == does
            == 0, as an example. */
#define EQUALITY_COMPARE_OP(OP, STROP) \
LOAD_CHECKED_REG(lhs_reg, code_pos, 2) \
LOAD_CHECKED_REG(rhs_reg, code_pos, 3) \
if (lhs_reg->sig->cls->id == SYM_CLASS_NUMBER) { \
    if (rhs_reg->sig->cls->id == SYM_CLASS_NUMBER) \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.number OP rhs_reg->value.number); \
    else \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.number OP rhs_reg->value.integer); \
} \
else if (lhs_reg->sig->cls->id == SYM_CLASS_INTEGER) { \
    if (rhs_reg->sig->cls->id == SYM_CLASS_INTEGER) \
        vm_regs[code[code_pos+4]]->value.integer =  \
        (lhs_reg->value.integer OP rhs_reg->value.integer); \
    else \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.integer OP rhs_reg->value.number); \
} \
else if (lhs_reg->sig->cls->id == SYM_CLASS_STR) { \
    vm_regs[code[code_pos+4]]->value.integer = \
    strcmp(lhs_reg->value.str->str, \
           rhs_reg->value.str->str) STROP; \
} \
else if (lhs_reg->sig == rhs_reg->sig) { \
    vm_regs[code[code_pos+4]]->value.integer = \
    /* Use int compare as pointer compare. A bit evil. */ \
    lhs_reg->value.integer == rhs_reg->value.integer; \
} \
vm_regs[code[code_pos+4]]->flags &= ~SYM_IS_NIL; \
code_pos += 5;

#define COMPARE_OP(OP, STROP) \
LOAD_CHECKED_REG(lhs_reg, code_pos, 2) \
LOAD_CHECKED_REG(rhs_reg, code_pos, 3) \
if (lhs_reg->sig->cls->id == SYM_CLASS_NUMBER) { \
    if (rhs_reg->sig->cls->id == SYM_CLASS_NUMBER) \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.number OP rhs_reg->value.number); \
    else \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.number OP rhs_reg->value.integer); \
} \
else if (lhs_reg->sig->cls->id == SYM_CLASS_INTEGER) { \
    if (rhs_reg->sig->cls->id == SYM_CLASS_INTEGER) \
        vm_regs[code[code_pos+4]]->value.integer =  \
        (lhs_reg->value.integer OP rhs_reg->value.integer); \
    else \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.integer OP rhs_reg->value.number); \
} \
else if (lhs_reg->sig->cls->id == SYM_CLASS_STR) { \
    vm_regs[code[code_pos+4]]->value.integer = \
    strcmp(lhs_reg->value.str->str, \
           rhs_reg->value.str->str) STROP; \
} \
vm_regs[code[code_pos+4]]->flags &= ~SYM_IS_NIL; \
code_pos += 5;

/* This intentionally takes the input sym as 'to' and the flags for 'from'. It
   does that so accidentally reversing the arguments will trigger a compile
   error instead of working. This also helps to make what's being set a little
   more obvious, since there is only one sym given. */
#define COPY_NIL_FLAG(to, from) \
to->flags = (to->flags & ~SYM_IS_NIL) ^ (from & SYM_IS_NIL);

/** vm init and deletion **/
lily_vm_state *lily_new_vm_state(lily_raiser *raiser)
{
    lily_vm_state *vm = lily_malloc(sizeof(lily_vm_state));
    if (vm == NULL)
        return NULL;

    /* todo: This is a terrible, horrible key to use. Make a better one using
             some randomness or...something. Just not this. */
    char sipkey[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};

    vm->method_stack = lily_malloc(sizeof(lily_vm_stack_entry *) * 4);
    vm->err_function = NULL;
    vm->in_function = 0;
    vm->method_stack_pos = 0;
    vm->raiser = raiser;
    vm->main = NULL;
    vm->vm_regs = NULL;
    vm->regs_from_main = NULL;
    vm->num_registers = 0;
    vm->max_registers = 0;
    vm->gc_live_entries = NULL;
    vm->gc_spare_entries = NULL;
    vm->gc_live_entry_count = 0;
    vm->gc_threshold = 100;
    vm->sipkey = sipkey;
    vm->gc_pass = 0;

    if (vm->method_stack) {
        int i;
        for (i = 0;i < 4;i++) {
            vm->method_stack[i] = lily_malloc(sizeof(lily_vm_stack_entry));
            if (vm->method_stack[i] == NULL)
                break;
        }
        vm->method_stack_size = i;
    }
    else
        vm->method_stack_size = 0;

    if (vm->method_stack == NULL || vm->method_stack_size != 4) {
        lily_free_vm_state(vm);
        return NULL;
    }

    return vm;
}

void lily_vm_free_registers(lily_vm_state *vm)
{
    lily_vm_register **regs_from_main = vm->regs_from_main;
    lily_vm_register *reg;
    int i;
    for (i = vm->max_registers-1;i >= 0;i--) {
        reg = regs_from_main[i];

        if (reg->sig->cls->is_refcounted && (reg->flags & SYM_IS_NIL) == 0)
            lily_deref_unknown_val(reg->sig, reg->value);

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
    for (i = 0;i < vm->method_stack_size;i++)
        lily_free(vm->method_stack[i]);

    /* Force an invoke, even if it's not time. This should clear everything that
       is still left. */
    lily_vm_invoke_gc(vm);

    lily_vm_destroy_gc(vm);

    lily_free(vm->method_stack);
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
    4: Finally, destroy the lists, objects, etc. that stage 2 didn't clear.
       Absolutely nothing is using these now, so it's safe to destroy them.

    vm: The vm to invoke the gc of. */
void lily_vm_invoke_gc(lily_vm_state *vm)
{
    /* This is (sort of) a mark-and-sweep garbage collector. This is called when
       a certain number of allocations have been done. Take note that values
       can be destroyed by deref. However, those values will have the gc_entry's
       value set to NULL as an indicator. */
    vm->gc_pass++;

    lily_vm_register **regs_from_main = vm->regs_from_main;
    int pass = vm->gc_pass;
    int num_registers = vm->num_registers;
    int i;
    lily_gc_entry *gc_iter;

    /* Stage 1: Go through all registers and use the appropriate gc_marker call
                that will mark every inner value that's visible. */
    for (i = 0;i < vm->num_registers;i++) {
        lily_vm_register *reg = regs_from_main[i];
        if ((reg->sig->flags & SIG_MAYBE_CIRCULAR) &&
            (reg->flags & SYM_IS_NIL) == 0 &&
             reg->value.gc_generic->gc_entry != NULL) {
            (*reg->sig->cls->gc_marker)(pass, reg->sig, reg->value);
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
            lily_vm_register *reg = regs_from_main[i];
            if ((reg->sig->flags & SIG_MAYBE_CIRCULAR) &&
                (reg->flags & SYM_IS_NIL) == 0 &&
                /* Not sure if this next line is necessary though... */
                reg->value.gc_generic->gc_entry != NULL &&
                reg->value.gc_generic->gc_entry->last_pass == -1) {
                reg->flags |= SYM_IS_NIL;
            }
        }
    }

    /* Stage 4: Delete the lists/objects/etc. that stage 2 didn't delete.
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
/* grow_method_stack
   This function grows the vm's method stack so it can take more method info.
   Calls lily_raise_nomem if unable to create method info. */
static void grow_method_stack(lily_vm_state *vm)
{
    int i;
    lily_vm_stack_entry **new_stack;

    /* Methods are free'd from 0 to stack_size, so don't increase stack_size
       just yet. */
    new_stack = lily_realloc(vm->method_stack,
            sizeof(lily_vm_stack_entry *) * 2 * vm->method_stack_size);

    if (new_stack == NULL)
        lily_raise_nomem(vm->raiser);

    vm->method_stack = new_stack;
    vm->method_stack_size *= 2;

    for (i = vm->method_stack_pos+1;i < vm->method_stack_size;i++) {
        vm->method_stack[i] = lily_malloc(sizeof(lily_vm_stack_entry));
        if (vm->method_stack[i] == NULL) {
            vm->method_stack_size = i;
            lily_raise_nomem(vm->raiser);
        }
    }
}

/* maybe_crossover_assign
   This handles assignment between two symbols which don't have the exact same
   type. This assumes the caller has verified that rhs is not nil.
   Returns 1 if the assignment happened, 0 otherwise. */
int maybe_crossover_assign(lily_vm_register *lhs_reg, lily_vm_register *rhs_reg)
{
    int ret = 1;

    if (rhs_reg->sig->cls->id != SYM_CLASS_OBJECT) {
        if (lhs_reg->sig->cls->id == SYM_CLASS_INTEGER &&
            rhs_reg->sig->cls->id == SYM_CLASS_NUMBER)
            lhs_reg->value.integer = (int64_t)(rhs_reg->value.number);
        else if (lhs_reg->sig->cls->id == SYM_CLASS_NUMBER &&
                 rhs_reg->sig->cls->id == SYM_CLASS_INTEGER)
            lhs_reg->value.number = (double)(rhs_reg->value.integer);
        else
            ret = 0;
    }
    else {
        lily_value obj_val = rhs_reg->value.object->value;
        int obj_class_id = rhs_reg->value.object->sig->cls->id;

        if (lhs_reg->sig->cls->id == SYM_CLASS_INTEGER &&
            obj_class_id == SYM_CLASS_NUMBER)
            lhs_reg->value.integer = (int64_t)(obj_val.number);
        else if (lhs_reg->sig->cls->id == SYM_CLASS_NUMBER &&
                 obj_class_id == SYM_CLASS_INTEGER)
            lhs_reg->value.number = (double)(obj_val.integer);
        else
            ret = 0;
    }

    if (ret)
        lhs_reg->flags &= ~SYM_IS_NIL;

    return ret;
}

/* novalue_error
   This is a helper routine that raises ErrNoValue because the given sym is
   nil but should not be. code_pos is the current code position, because the
   current method's info is not saved in the stack (because it would almost
   always be stale). */
static void novalue_error(lily_vm_state *vm, int code_pos, int reg_pos)
{
    /* ...So fill in the current method's info before dying. */
    lily_vm_stack_entry *top = vm->method_stack[vm->method_stack_pos-1];
    /* Methods do not have a linetable that maps opcodes to line numbers.
       Instead, the emitter writes the line number right after the opcode for
       any opcode that might call novalue_error. */ 
    top->line_num = top->code[code_pos+1];

    /* Instead of using the register, grab the register info for the current
       method. This will have the name, if this particular register is used to
       hold a named var. */
    lily_register_info *reg_info;
    reg_info = vm->method_stack[vm->method_stack_pos - 1]->method->reg_info;

    /* A method's register info and the registers are the same size. The info at
       position one is for the first register, the second for the second
       register, etc. */
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
    Note: This is intentionally not called by subscript assign so that assigning
          to a non-existant part of a hash automatically adds that key.

    vm:       The currently running vm.
    code_pos: The start of the opcode, for getting line info.
    key_sig:  The signature of the key given. This is used to display a nice
              value for the key.
    key:      The invalid key passed. */
void no_such_key_error(lily_vm_state *vm, int code_pos, lily_sig *key_sig,
        lily_value key)
{
    lily_vm_stack_entry *top = vm->method_stack[vm->method_stack_pos - 1];
    top->line_num = top->code[code_pos + 1];

    lily_msgbuf *msgbuf = vm->raiser->msgbuf;

    lily_msgbuf_add(msgbuf, "ErrNoSuchKey: ");
    if (key_sig->cls->id == SYM_CLASS_INTEGER)
        lily_msgbuf_add_int(msgbuf, key.integer);
    else if (key_sig->cls->id == SYM_CLASS_NUMBER)
        lily_msgbuf_add_double(msgbuf, key.number);
    else if (key_sig->cls->id == SYM_CLASS_STR) {
        lily_msgbuf_add_char(msgbuf, '\"');
        /* Note: This is fine for now because strings can't contain \0. */
        lily_msgbuf_add(msgbuf, key.str->str);
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
    lily_vm_stack_entry *top = vm->method_stack[vm->method_stack_pos-1];
    top->line_num = top->code[code_pos+1];

    lily_raise(vm->raiser, lily_ErrDivideByZero,
            "Attempt to divide by zero.\n");
}

/* boundary_error
   Another copy of novalue_error, this one raising ErrOutOfRange. */
void boundary_error(lily_vm_state *vm, int code_pos, int bad_index)
{
    lily_vm_stack_entry *top = vm->method_stack[vm->method_stack_pos-1];
    top->line_num = top->code[code_pos+1];

    lily_raise(vm->raiser, lily_ErrOutOfRange,
            "Subscript index %d is out of range.\n", bad_index);
}

/* lily_builtin_print
   This is called by the vm to implement the print function. [0] is the return
   (which isn't used), so args begin at [1]. */
void lily_builtin_print(lily_vm_state *vm, uintptr_t *code, int num_args)
{
    lily_vm_register *reg = vm->vm_regs[code[0]];
    lily_impl_send_html(reg->value.str->str);
}

/* lily_builtin_printfmt
   This is called by the vm to implement the printfmt function. [0] is the
   return, which is ignored in this case. [1] is the format string, and [2]+
   are the arguments. */
void lily_builtin_printfmt(lily_vm_state *vm, uintptr_t *code, int num_args)
{
    char fmtbuf[64];
    char save_ch;
    char *fmt, *str_start;
    int cls_id, is_nil;
    int arg_pos = 0, i = 0;
    lily_vm_register **vm_regs = vm->vm_regs;
    lily_vm_register *arg;
    lily_value val;

    fmt = vm_regs[code[0]]->value.str->str;
    str_start = fmt;
    while (1) {
        if (fmt[i] == '\0')
            break;
        else if (fmt[i] == '%') {
            if (arg_pos == num_args)
                return;

            save_ch = fmt[i];
            fmt[i] = '\0';
            lily_impl_send_html(str_start);
            fmt[i] = save_ch;
            i++;

            arg = vm_regs[code[arg_pos + 1]];
            cls_id = arg->value.object->sig->cls->id;
            val = arg->value.object->value;
            is_nil = 0;

            if (fmt[i] == 'i') {
                if (cls_id != SYM_CLASS_INTEGER)
                    return;
                if (is_nil)
                    lily_impl_send_html("(nil)");
                else {
                    snprintf(fmtbuf, 63, "%" PRId64, val.integer);
                    lily_impl_send_html(fmtbuf);
                }
            }
            else if (fmt[i] == 's') {
                if (cls_id != SYM_CLASS_STR)
                    return;
                if (is_nil)
                    lily_impl_send_html("(nil)");
                else
                    lily_impl_send_html(val.str->str);
            }
            else if (fmt[i] == 'n') {
                if (cls_id != SYM_CLASS_NUMBER)
                    return;

                if (is_nil)
                    lily_impl_send_html("(nil)");
                else {
                    snprintf(fmtbuf, 63, "%f", val.number);
                    lily_impl_send_html(fmtbuf);
                }
            }

            str_start = fmt + i + 1;
            arg_pos++;
        }
        i++;
    }

    lily_impl_send_html(str_start);
}
/** VM opcode helpers **/

/* op_ref_assign
   VM helper called for handling complex assigns. [1] is lhs, [2] is rhs. This
   does an assign along with the appropriate ref/deref stuff. This is suitable
   for anything that needs that ref/deref stuff except for object. */
void op_ref_assign(lily_vm_register *lhs_reg, lily_vm_register *rhs_reg)
{
    lily_value value = lhs_reg->value;

    if (!(lhs_reg->flags & SYM_IS_NIL))
        lily_deref_unknown_val(lhs_reg->sig, value);
    if (!(rhs_reg->flags & SYM_IS_NIL)) {
        rhs_reg->value.generic->refcount++;
        lhs_reg->flags &= ~SYM_IS_NIL;
    }
    else
        lhs_reg->flags |= SYM_IS_NIL;

    lhs_reg->value = rhs_reg->value;
}

/*  calculate_siphash
    Return a siphash based using the given siphash for the given key.

    sipkey:  The vm's sipkey for creating the hash.
    key_sig: The signature describing the key given.
    key:     The key to be hashed.

    Notes:
    * The caller must not pass a non-hashable type (such as object). Parser is
      responsible for ensuring that hashes only use valid key types.
    * The caller must not pass a key that is a nil value. */
static uint64_t calculate_siphash(char *sipkey, lily_sig *key_sig, lily_value key)
{
    int key_cls_id = key_sig->cls->id;
    uint64_t key_hash;

    if (key_cls_id == SYM_CLASS_STR)
        key_hash = siphash24(key.str->str, key.str->size, sipkey);
    else if (key_cls_id == SYM_CLASS_INTEGER)
        key_hash = key.integer;
    else if (key_cls_id == SYM_CLASS_NUMBER)
        /* siphash thinks it's sent a pointer (and will try to deref it), so
           send the address. */
        key_hash = siphash24(&(key.number), sizeof(double), sipkey);
    else /* Should not happen, because no other classes are valid keys. */
        key_hash = 0;

    return key_hash;
}

/*  try_lookup_hash_elem
    This attempts to find a hash element by key in the given hash. This will not
    create a new element if it fails.

    hash:        A valid hash, which may or may not have elements.
    key_siphash: The calculated siphash of the given key. Use calculate_siphash
                 to get this.
    key_sig:     A signature describing the type of the key.
    key:         The key used for doing the search.

    On success: The hash element that was inserted into the hash value is
                returned.
    On failure: NULL is returned. */
static lily_hash_elem *try_lookup_hash_elem(lily_hash_val *hash,
        uint64_t key_siphash, lily_sig *key_sig, lily_value key)
{
    int key_cls_id = key_sig->cls->id;

    lily_hash_elem *elem_iter = hash->elem_chain;
    while (elem_iter) {
        if (elem_iter->key_siphash == key_siphash) {
            int ok;

            if (key_cls_id == SYM_CLASS_INTEGER &&
                elem_iter->key.integer == key.integer)
                ok = 1;
            else if (key_cls_id == SYM_CLASS_NUMBER &&
                     elem_iter->key.number  == key.number)
                ok = 1;
            else if (key_cls_id == SYM_CLASS_STR &&
                    /* strings are immutable, so try a ptr compare first. */
                    ((elem_iter->key.str == key.str) ||
                     /* No? Make sure the sizes match, then call for a strcmp.
                        The size check is an easy way to potentially skip a
                        strcmp in case of hash collision. */
                      (elem_iter->key.str->size == key.str->size &&
                       strcmp(elem_iter->key.str->str, key.str->str) == 0)))
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

/*  try_get_make_hash_elem
    This attempts to get a hash element from a given hash. This will try to
    make a new hash element if none currently exists.

    hash:        A valid hash, which may or may not have elements.
    key_siphash: The calculated siphash of the given key. Use calculate_siphash
                 to get this.
    key_sig:     A signature describing the type of the key.
    key:         The key used for doing the search.

    This returns the newly-made element, or NULL if unable to make a new
    element. */
static lily_hash_elem *try_get_make_hash_elem(lily_hash_val *hash,
        uint64_t key_siphash, lily_sig *key_sig, lily_value key)
{
    lily_hash_elem *elem = try_lookup_hash_elem(hash, key_siphash, key_sig, key);

    if (elem == NULL) {
        elem = lily_try_new_hash_elem();
        if (elem != NULL) {
            elem->key = key;
            elem->key_siphash = key_siphash;

            elem->next = hash->elem_chain;
            hash->elem_chain = elem;

            hash->num_elems++;
        }
    }

    return elem;
}

static void op_subscript(lily_vm_state *vm, uintptr_t *code, int code_pos)
{
    lily_vm_register **vm_regs = vm->vm_regs;
    lily_vm_register *lhs_reg, *index_reg, *result_reg;
    int flags, index_int;
    lily_sig *new_sig;
    lily_value new_value;
    lily_hash_elem *hash_elem;

    lhs_reg = vm_regs[code[code_pos + 2]];
    LOAD_CHECKED_REG(index_reg, code_pos, 3)
    result_reg = vm_regs[code[code_pos + 4]];

    if (lhs_reg->sig->cls->id == SYM_CLASS_LIST) {
        LOAD_CHECKED_REG(lhs_reg, code_pos, 2)
        index_int = index_reg->value.integer;

        /* Too big! */
        if (index_int >= lhs_reg->value.list->num_values)
            boundary_error(vm, code_pos, index_int);

        /* todo: Wraparound would be nice. */
        if (index_int < 0)
            boundary_error(vm, code_pos, index_int);

        hash_elem = NULL;
        flags = lhs_reg->value.list->flags[index_int];
    }
    else {
        if (lhs_reg->flags & SYM_IS_NIL) {
            lily_hash_val *hv = lily_try_new_hash_val();
            if (hv == NULL)
                lily_raise_nomem(vm->raiser);

            lhs_reg->value.hash = hv;
            lhs_reg->flags &= ~SYM_IS_NIL;
        }
        uint64_t siphash;
        siphash = calculate_siphash(vm->sipkey, index_reg->sig,
                index_reg->value);
        hash_elem = try_lookup_hash_elem(lhs_reg->value.hash,
                siphash, index_reg->sig, index_reg->value);

        /* Give up if the key doesn't exist. */
        if (hash_elem == NULL)
            no_such_key_error(vm, code_pos, index_reg->sig, index_reg->value);

        index_int = 0;
        flags = hash_elem->flags;
    }

    if (result_reg->sig->cls->id == SYM_CLASS_OBJECT) {
        if ((flags & SYM_IS_NIL) == 0) {
            lily_object_val *ov;
            if (hash_elem == NULL)
                ov = lhs_reg->value.list->values[index_int].object;
            else
                ov = hash_elem->value.object;

            new_sig = ov->sig;
            new_value = ov->value;

            /* Make an object to hold a value if necessary. This is best done
               before doing the ref, so that the ref doesn't have to be undone
               if there is an issue. */
            if (result_reg->flags & SYM_IS_NIL) {
                lily_object_val *new_ov = lily_try_new_object_val();
                if (new_ov == NULL ||
                    lily_try_add_gc_item(vm, result_reg->sig,
                            (lily_generic_gc_val *)new_ov) == 0) {
                    lily_free(new_ov);
                    lily_raise_nomem(vm->raiser);
                }

                result_reg->value.object = new_ov;
                result_reg->flags &= ~SYM_IS_NIL;
            }
        }
        else {
            new_sig = vm->integer_sig;
            new_value.integer = 0;
        }
    }
    else {
        new_sig = result_reg->sig;
        if ((flags & SYM_IS_NIL) == 0) {
            if (hash_elem == NULL)
                new_value = lhs_reg->value.list->values[index_int];
            else
                new_value = hash_elem->value;
        }
    }

    /* Give the new value a ref if it needs one. This should be done before
       doing the deref to be consistent with other parts of the vm. */
    if (new_sig &&
        new_sig->cls->is_refcounted &&
        ((flags & SYM_IS_NIL) == 0))
        new_value.generic->refcount++;

    if (result_reg->sig->cls->id == SYM_CLASS_OBJECT) {
        if ((flags & SYM_IS_NIL) == 0) {
            /* If the value isn't nil, then an object was made above to hold
               the new value. */
            lily_object_val *ov = result_reg->value.object;

            if (ov->sig &&
                ov->sig->cls->is_refcounted &&
                (result_reg->flags & SYM_IS_NIL) == 0)
                lily_deref_unknown_val(ov->sig, ov->value);

            ov->sig = new_sig;
            ov->value = new_value;
        }
        else {
            if ((result_reg->flags & SYM_IS_NIL) == 0) {
                result_reg->value.object->sig = NULL;
                result_reg->value.object->value.integer = 0;
            }
        }
    }
    else {
        /* If the result contains a ref'd value, then deref it. */
        if (result_reg->sig->cls->is_refcounted &&
            (result_reg->flags & SYM_IS_NIL) == 0)
            lily_deref_unknown_val(result_reg->sig, result_reg->value);

        if ((flags & SYM_IS_NIL) == 0) {
            result_reg->flags &= ~SYM_IS_NIL;
            result_reg->value = new_value;
        }
        else
            result_reg->flags |= SYM_IS_NIL;
    }
}

/* op_sub_assign
   This handles the o_sub_assign opcode for the vm. This first unpacks the 2, 3,
   and 4 as the lhs, the index, and the rhs. Once it checks to make sure the
   index is valid, then 'lhs[index] = rhs' is performed. This is a bit complex
   because it has to handle any kind of assign.
   Sometimes, value will be a storage from o_subscript. However, this code is
   required because it assigns to the value in the list, instead of a storage
   where that value is unloaded. */
static void op_sub_assign(lily_vm_state *vm, uintptr_t *code, int code_pos)
{
    lily_vm_register **vm_regs = vm->vm_regs;
    lily_vm_register *lhs_reg, *rhs_reg;
    lily_vm_register *index_reg;
    int index_int;
    lily_value *list_values;
    int flags, new_flags;
    lily_sig *result_sig;
    lily_hash_elem *hash_elem;
    lily_value old_value;

    lhs_reg = vm_regs[code[code_pos + 2]];
    LOAD_CHECKED_REG(index_reg, code_pos, 3)

    if (lhs_reg->sig->cls->id == SYM_CLASS_LIST) {
        LOAD_CHECKED_REG(lhs_reg, code_pos, 2)
        index_int = index_reg->value.integer;

        if (index_int >= lhs_reg->value.list->num_values)
            boundary_error(vm, code_pos, index_int);

        /* todo: Wraparound would be nice. */
        if (index_int < 0)
            boundary_error(vm, code_pos, index_int);

        list_values = lhs_reg->value.list->values;
        hash_elem = NULL;
        result_sig = lhs_reg->sig->siglist[0];
        flags = lhs_reg->value.list->flags[index_int];
        old_value = list_values[index_int];
    }
    else {
        if (lhs_reg->flags & SYM_IS_NIL) {
            lily_hash_val *hv = lily_try_new_hash_val();
            if (hv == NULL)
                lily_raise_nomem(vm->raiser);

            lhs_reg->value.hash = hv;
            lhs_reg->flags &= ~SYM_IS_NIL;
        }
        uint64_t siphash;
        siphash = calculate_siphash(vm->sipkey, index_reg->sig,
                index_reg->value);
        hash_elem = try_get_make_hash_elem(lhs_reg->value.hash,
                siphash, index_reg->sig, index_reg->value);
        if (hash_elem == NULL)
            lily_raise_nomem(vm->raiser);

        index_int = 0;
        list_values = NULL;
        result_sig = lhs_reg->sig->siglist[1];
        flags = hash_elem->flags;
        old_value = hash_elem->value;
    }

    LOAD_CHECKED_REG(rhs_reg, code_pos, 4)
    new_flags = flags & ~SYM_IS_NIL;

    /* If this list does not contain objects, then standard
       assign or ref/deref assign is enough. */
    if (result_sig->cls->id != SYM_CLASS_OBJECT) {
        if (rhs_reg->sig->cls->is_refcounted) {
            /* Deref the old value as long as it's not nil. */
            if ((flags & SYM_IS_NIL) == 0)
                lily_deref_unknown_val(result_sig, old_value);

            /* Now give the new value a ref bump and fall down to assignment. */
            rhs_reg->value.generic->refcount++;
        }
        /* else nothing to ref/deref, so do assign only. */

        old_value = rhs_reg->value;
    }
    else {
        /* Do an object assign to the value. */
        lily_object_val *ov;
        if (flags & SYM_IS_NIL) {
            /* Objects are containers, so make one to receive the value that is
               coming. */
            ov = lily_try_new_object_val();
            if (ov == NULL ||
                lily_try_add_gc_item(vm, result_sig,
                        (lily_generic_gc_val *)ov) == 0) {
                lily_free(ov);
                lily_raise_nomem(vm->raiser);
            }

            ov->value.integer = 0;
            ov->refcount = 1;
            ov->sig = NULL;
        }
        else {
            if (hash_elem == NULL)
                ov = list_values[index_int].object;
            else
                ov = hash_elem->value.object;
        }

        lily_sig *rhs_sig;
        lily_value rhs_value;
        if (rhs_reg->sig->cls->id != SYM_CLASS_OBJECT) {
            rhs_sig = rhs_reg->sig;
            rhs_value = rhs_reg->value;
        }
        else {
            rhs_sig = rhs_reg->value.object->sig;
            rhs_value = rhs_reg->value.object->value;
        }

        if (ov->sig && ov->sig->cls->is_refcounted)
            lily_deref_unknown_val(ov->sig, ov->value);

        ov->sig = rhs_sig;
        ov->value = rhs_value;
        old_value.object = ov;

        if (rhs_sig && rhs_sig->cls->is_refcounted)
            rhs_value.generic->refcount++;
    }

    if (hash_elem == NULL) {
        lhs_reg->value.list->flags[index_int] = new_flags;
        lhs_reg->value.list->values[index_int] = old_value;
    }
    else {
        hash_elem->flags = new_flags;
        hash_elem->value = old_value;
    }
}

/*  op_object_assign
    This is a vm helper for handling an assignment to an object from another
    value that may or may not be an object.
    Since this call only uses two values, those are passed instead of using
    vm_regs and code like some other vm helpers do.
    vm:      If lhs_reg is nil, an object will be made that needs a gc entry.
             The entry will be added to the vm's gc entries.
    lhs_reg: The register containing an object to be assigned to. Might be nil.
    rhs_reg: The register providing a value for the object. Might be nil. */
static void op_object_assign(lily_vm_state *vm, lily_vm_register *lhs_reg,
        lily_vm_register *rhs_reg)
{
    lily_value right_val;
    lily_sig *right_sig;

    /* If the right side has no value, mark the left's sig as
       null. This way, the object value doesn't have to be
       free'd. */
    if (rhs_reg->sig->cls->id == SYM_CLASS_OBJECT) {
        if (rhs_reg->flags & SYM_IS_NIL ||
            rhs_reg->value.object->sig == NULL) {
            right_val.integer = 0;
            right_sig = vm->integer_sig;
        }
        else {
            lily_object_val *rhs_obj = rhs_reg->value.object;
            if (rhs_obj->sig->cls->is_refcounted)
                rhs_obj->value.generic->refcount++;

            right_val = rhs_obj->value;
            right_sig = rhs_obj->sig;
        }
    }
    else {
        /* object = non-object */
        if (rhs_reg->sig->cls->is_refcounted)
            rhs_reg->value.generic->refcount++;

        right_val = rhs_reg->value;
        right_sig = rhs_reg->sig;
    }

    lily_object_val *lhs_obj;

    /* If the lhs register is nil, allocate an object val for
       it. */
    if (lhs_reg->flags & SYM_IS_NIL) {
        lhs_obj = lily_try_new_object_val();
        if (lhs_obj == NULL ||
            lily_try_add_gc_item(vm, lhs_reg->sig,
                    (lily_generic_gc_val *)lhs_obj) == 0) {
            /* Something above may have done a ref, but never
               assigned. Undo that. */
            if (right_sig->cls->is_refcounted)
                right_val.generic->refcount--;

            lily_free(lhs_obj);
            lily_raise_nomem(vm->raiser);
        }

        lhs_reg->value.object = lhs_obj;
        lhs_reg->flags &= ~SYM_IS_NIL;
    }
    else {
        /* Deref what the object contains if the value is
           refcounted. */
        lhs_obj = lhs_reg->value.object;

        if (lhs_obj->sig != NULL &&
            lhs_obj->sig->cls->is_refcounted) {
            lily_deref_unknown_val(lhs_obj->sig,
                    lhs_obj->value);
        }
    }

    lhs_obj->sig = right_sig;
    lhs_obj->value = right_val;
}

/* op_build_list
   VM helper called for handling o_build_list. This is a bit tricky, becaus the
   storage may have already had a previous list assigned to it. Additionally,
   the new list info may fail to allocate. If it does, ErrNoMem is raised. */
void op_build_list(lily_vm_state *vm, lily_vm_register **vm_regs,
        uintptr_t *code)
{
    int num_elems = (intptr_t)(code[2]);
    int j;
    lily_vm_register *result = vm_regs[code[3+num_elems]];
    lily_sig *elem_sig = result->sig->siglist[0];

    lily_list_val *lv = lily_malloc(sizeof(lily_list_val));
    if (lv == NULL)
        lily_raise_nomem(vm->raiser);

    /* This is set in case the gc looks at this list. This prevents the gc and
       deref calls from touching ->values and ->flags. */
    lv->num_values = -1;
    lv->visited = 0;
    lv->refcount = 1;
    lv->values = lily_malloc(num_elems * sizeof(void *));
    lv->flags = lily_malloc(num_elems * sizeof(int));
    lv->gc_entry = NULL;

    /* If attaching a gc entry fails, then list deref will collect everything
       which is what's wanted anyway. */
    if (lv->values == NULL || lv->flags == NULL ||
        ((result->sig->flags & SIG_MAYBE_CIRCULAR) &&
          lily_try_add_gc_item(vm, result->sig,
                (lily_generic_gc_val *)lv) == 0)) {

        lily_deref_list_val(result->sig, lv);
        lily_raise_nomem(vm->raiser);
    }

    /* The old value can be destroyed, now that the new value has been made. */
    if (!(result->flags & SYM_IS_NIL))
        lily_deref_list_val(result->sig, result->value.list);

    /* Put the new list in the register so the gc doesn't try to collect it. */
    result->value.list = lv;
    /* And make sure it's not marked as nil, or the gc will think that anything
       inside of it is unreachable. That's really bad for list[object]. */
    result->flags &= ~SYM_IS_NIL;

    /* This could be condensed down, but doing it this way improves speed since
       the elem_sig won't change over the loop. */
    if (elem_sig->cls->id != SYM_CLASS_OBJECT) {
        if (elem_sig->cls->is_refcounted) {
            for (j = 0;j < num_elems;j++) {
                if (!(vm_regs[code[3+j]]->flags & SYM_IS_NIL)) {
                    lv->values[j] = vm_regs[code[3+j]]->value;
                    /* The elements are refcounted, so give a ref bump to each
                       element scanned in as well as copying the value. */
                    lv->values[j].generic->refcount++;
                    lv->flags[j] = 0;
                }
                else
                    lv->flags[j] = SYM_IS_NIL;
            }
        }
        else {
            for (j = 0;j < num_elems;j++) {
                if (!(vm_regs[code[3+j]]->flags & SYM_IS_NIL)) {
                    lv->values[j] = vm_regs[code[3+j]]->value;
                    lv->flags[j] = 0;
                }
                else
                    lv->flags[j] = SYM_IS_NIL;
            }
        }
    }
    else {
        for (j = 0;j < num_elems;j++) {
            if (!(vm_regs[code[3+j]]->flags & SYM_IS_NIL)) {
                /* Objects are supposed to act like containers which can hold
                   any value. Because of this, the inner value of the other
                   object must be copied over.
                   Objects are also potentially circular, which means each one
                   needs a gc entry. This also means that the list holding the
                   objects has a gc entry. */
                lily_object_val *oval = lily_try_new_object_val();
                lily_object_val *reg_object = vm_regs[code[3+j]]->value.object;
                if (oval == NULL ||
                    lily_try_add_gc_item(vm, elem_sig,
                            (lily_generic_gc_val *)oval) == 0) {
                    /* Make sure to free the object value made. If it wasn't
                       made, this will be NULL, which is fine. */
                    lily_free(oval);

                    /* This one's nil. */
                    lv->flags[j] = SYM_IS_NIL;

                    /* Give up. The gc will have an entry for the list, so it
                       will correctly collect the list. */
                    lily_raise_nomem(vm->raiser);
                }
                oval->value = reg_object->value;
                oval->sig = reg_object->sig;
                oval->refcount = 1;

                if (oval->sig && oval->sig->cls->is_refcounted)
                    oval->value.generic->refcount++;

                lv->values[j].object = oval;
                lv->flags[j] = 0;
            }
            else
                lv->flags[j] = SYM_IS_NIL;

            /* Update this with each pass, in case the vm decides to run the gc
               when a new object asks for a gc_entry. */
            lv->num_values = j + 1;
        }
    }

    lv->num_values = num_elems;
}

static void do_keyword_show(lily_vm_state *vm, int is_global, int reg_id)
{
    lily_vm_register *reg;
    lily_method_val *lily_main;
    lily_method_val *current_method;

    if (is_global)
        reg = vm->regs_from_main[reg_id];
    else
        reg = vm->vm_regs[reg_id];

    lily_main = vm->method_stack[0]->method;
    current_method = vm->method_stack[vm->method_stack_pos - 1]->method;
    lily_show_sym(lily_main, current_method, reg, is_global, reg_id,
            vm->raiser->msgbuf);
}

/** vm registers handling and stack growing **/

/* grow_vm_registers
   Increase the amount of registers available to the given 'register_need'.  */
static void grow_vm_registers(lily_vm_state *vm, int register_need)
{
    lily_vm_register **new_regs;
    lily_sig *integer_sig = vm->integer_sig;
    int i = vm->max_registers;

    ptrdiff_t reg_offset = vm->vm_regs - vm->regs_from_main;

    /* Remember, use regs_from_main, NOT vm_regs, which is likely adjusted. */
    new_regs = lily_realloc(vm->regs_from_main, register_need *
            sizeof(lily_vm_register));

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
        new_regs[i] = lily_malloc(sizeof(lily_vm_register));
        if (new_regs[i] == NULL) {
            vm->max_registers = i;
            lily_raise_nomem(vm->raiser);
        }

        new_regs[i]->sig = integer_sig;
        new_regs[i]->flags = SYM_IS_NIL;
    }

    vm->max_registers = register_need;
}

/* prep_registers
   This sets up the registers for a method call. This is called after
   grow_registers, and thus assumes that the right number of registers are
   available.
   This also handles copying args for methods. */
static void prep_registers(lily_vm_state *vm, lily_method_val *mval,
        uintptr_t *code)
{
    lily_vm_register **vm_regs = vm->vm_regs;
    lily_vm_register **regs_from_main = vm->regs_from_main;
    lily_register_info *register_seeds = mval->reg_info;
    int num_registers = vm->num_registers;
    int register_need = vm->num_registers + mval->reg_count;
    int i;

    /* A method's args always come first, so copy arguments over while clearing
       old values. */
    for (i = 0;i < code[3];i++, num_registers++) {
        lily_register_info seed = register_seeds[i];
        lily_vm_register *get_reg = vm_regs[code[4+i]];
        lily_vm_register *set_reg = regs_from_main[num_registers];

        /* The get must be run before the set. Otherwise, if
           something has 1 ref and assigns to itself, it will be
           destroyed from a deref, then an invalid value ref'd.
           This may not be possible here, but it is elsewhere. */
        if (get_reg->sig->cls->is_refcounted &&
            ((get_reg->flags & SYM_IS_NIL) == 0))
            get_reg->value.generic->refcount++;

        if (set_reg->sig->cls->is_refcounted &&
            ((set_reg->flags & SYM_IS_NIL) == 0))
            lily_deref_unknown_val(set_reg->sig, set_reg->value);

        set_reg->sig = seed.sig;
        /* This will be null if this register doesn't belong to a
           var, or non-null if it's for a local. */

        if ((get_reg->flags & SYM_IS_NIL) == 0)
            set_reg->value = get_reg->value;
        else
            set_reg->value.integer = 0;

        COPY_NIL_FLAG(set_reg, get_reg->flags)
    }

    /* For the rest of the registers, clear whatever value they have. */
    for (;num_registers < register_need;i++, num_registers++) {
        lily_register_info seed = mval->reg_info[i];

        lily_vm_register *reg = regs_from_main[num_registers];
        if (reg->sig->cls->is_refcounted &&
            (reg->flags & SYM_IS_NIL) == 0)
            lily_deref_unknown_val(reg->sig, reg->value);

        reg->flags |= SYM_IS_NIL;
        reg->sig = seed.sig;
    }

    vm->num_registers = num_registers;
}

/* load_vm_regs
   This is a helper for lily_vm_prep that loads a chain of vars into the
   registers given. */
static void load_vm_regs(lily_vm_register **vm_regs, lily_var *iter_var)
{
    while (iter_var) {
        if ((iter_var->flags & SYM_IS_NIL) == 0) {
            if (iter_var->sig->cls->is_refcounted)
                iter_var->value.generic->refcount++;

            vm_regs[iter_var->reg_spot]->flags &= ~SYM_IS_NIL;
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
    lily_method_val *main_method = main_var->value.method;
    lily_var *global_iter = main_var;
    int i;

    lily_vm_register **vm_regs;
    vm_regs = lily_malloc(main_method->reg_count * sizeof(lily_vm_register *));
    if (vm_regs == NULL)
        lily_raise_nomem(vm->raiser);

    vm->regs_from_main = vm_regs;
    for (i = 0;i < main_method->reg_count;i++) {
        lily_vm_register *reg = lily_malloc(sizeof(lily_vm_register));
        if (reg == NULL) {
            vm->max_registers = i;
            lily_raise_nomem(vm->raiser);
            continue;
        }

        vm_regs[i] = reg;
        lily_register_info seed = main_method->reg_info[i];

        /* This allows o_assign to copy data over without having to check for
           a nil flag. */
        reg->value.integer = 0;
        reg->flags = SYM_IS_NIL | SYM_SCOPE_GLOBAL;
        reg->sig = seed.sig;
    }

    while (global_iter) {
        if ((global_iter->flags & SYM_IS_NIL) == 0) {
            if (global_iter->sig->cls->is_refcounted)
                global_iter->value.generic->refcount++;

            vm_regs[global_iter->reg_spot]->flags &= ~SYM_IS_NIL;
            vm_regs[global_iter->reg_spot]->value = global_iter->value;
        }

        global_iter = global_iter->next;
    }

    for (i = 0;i < symtab->class_pos;i++) {
        lily_class *cls = symtab->classes[i];
        if (cls->call_start != NULL)
            load_vm_regs(vm_regs, cls->call_start);
    }

    if (symtab->old_method_chain != NULL)
        load_vm_regs(vm_regs, symtab->old_method_chain);

    vm->main = main_var;
    vm->num_registers = main_method->reg_count;
    vm->max_registers = main_method->reg_count;
    vm->vm_regs = vm_regs;
    vm->regs_from_main = vm_regs;
    lily_class *integer_cls = lily_class_by_id(symtab, SYM_CLASS_INTEGER);
    vm->integer_sig = integer_cls->sig;

    lily_vm_stack_entry *stack_entry = vm->method_stack[0];
    stack_entry->method = main_method;
    stack_entry->code = main_method->code;
    stack_entry->regs_used = main_method->reg_count;
    vm->method_stack_pos = 1;
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
    lily_method_val *m = vm->main->value.method;
    uintptr_t *code = m->code;
    lily_vm_stack_entry *stack_entry;
    lily_vm_register **regs_from_main;
    lily_vm_register **vm_regs;
    int i, num_registers, max_registers;
    lily_sig *cast_sig;
    register int64_t for_temp;
    register int code_pos;
    register lily_vm_register *lhs_reg, *rhs_reg, *loop_reg, *step_reg;
    register lily_literal *literal;
    lily_method_val *mval;

    /* Initialize local vars from the vm state's vars. */
    vm_regs = vm->vm_regs;
    regs_from_main = vm->regs_from_main;
    num_registers = vm->num_registers;
    max_registers = vm->max_registers;
    code_pos = 0;

    if (setjmp(vm->raiser->jumps[vm->raiser->jump_pos]) == 0)
        vm->raiser->jump_pos++;
    else {
        /* The vm doesn't set ->err_function when it calls because it assumes
           most functions won't have an error. So if there is one, set this for
           a proper stack. */
        if (vm->in_function)
            vm->err_function = vm_regs[code[code_pos+2]]->value.function;

        /* The top of the stack is always changing, so make sure the top's
           line number is set. This is safe because any opcode that can raise
           will also have a line number right after the opcode. */
        lily_vm_stack_entry *top = vm->method_stack[vm->method_stack_pos-1];
        top->line_num = top->code[code_pos+1];

        /* Don't yield to parser, because it will continue as if nothing
           happened. Instead, jump to where it would jump. */
        longjmp(vm->raiser->jumps[vm->raiser->jump_pos-2], 1);
    }

    while (1) {
        switch(code[code_pos]) {
            case o_assign:
                rhs_reg = vm_regs[code[code_pos+2]];
                lhs_reg = vm_regs[code[code_pos+3]];
                COPY_NIL_FLAG(lhs_reg, rhs_reg->flags)
                lhs_reg->value = rhs_reg->value;
                code_pos += 4;
                break;
            case o_get_const:
                literal = (lily_literal *)code[code_pos+2];
                lhs_reg = vm_regs[code[code_pos+3]];
                if (lhs_reg->sig->cls->is_refcounted) {
                    if ((lhs_reg->flags & SYM_IS_NIL) == 0)
                        lily_deref_unknown_val(lhs_reg->sig, lhs_reg->value);

                    literal->value.generic->refcount++;
                }
                lhs_reg->value = literal->value;
                lhs_reg->flags &= ~SYM_IS_NIL;
                code_pos += 4;
                break;
            case o_integer_add:
                INTEGER_OP(+)
                break;
            case o_integer_minus:
                INTEGER_OP(-)
                break;
            case o_number_add:
                INTNUM_OP(+)
                break;
            case o_number_minus:
                INTNUM_OP(-)
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
            case o_number_mul:
                INTNUM_OP(*)
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
            case o_number_div:
                /* This is a little more tricky, because the rhs could be a
                   number or an integer... */
                LOAD_CHECKED_REG(rhs_reg, code_pos, 3)
                if (rhs_reg->sig->cls->id == SYM_CLASS_INTEGER &&
                    rhs_reg->value.integer == 0)
                    divide_by_zero_error(vm, code_pos, 3);
                else if (rhs_reg->sig->cls->id == SYM_CLASS_NUMBER &&
                         rhs_reg->value.number == 0)
                    divide_by_zero_error(vm, code_pos, 3);

                INTNUM_OP(/)
                break;
            case o_jump_if:
                lhs_reg = vm_regs[code[code_pos+2]];
                {
                    int cls_id, result;

                    if ((lhs_reg->flags & SYM_IS_NIL) == 0) {
                        cls_id = lhs_reg->sig->cls->id;
                        if (cls_id == SYM_CLASS_INTEGER)
                            result = (lhs_reg->value.integer == 0);
                        else if (cls_id == SYM_CLASS_NUMBER)
                            result = (lhs_reg->value.number == 0);
                        else if (cls_id == SYM_CLASS_OBJECT) {
                            lily_object_val *ov = lhs_reg->value.object;
                            if (ov->sig->cls->id == SYM_CLASS_INTEGER)
                                result = (ov->value.integer == 0);
                            else if (ov->sig->cls->id == SYM_CLASS_NUMBER)
                                result = (ov->value.integer == 0);
                            else
                                result = 0;
                        }
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
            case o_func_call:
            {
                /* var, func, #args, ret, args... */
                lily_function_val *fval;
                lily_func func;
                int j = code[code_pos+3];
                lhs_reg = vm_regs[code[code_pos+2]];

                /* The func HAS to be grabbed from the var to support passing
                   funcs as args. */
                fval = (lily_function_val *)(lhs_reg->value.function);
                func = fval->func;

                vm->in_function = 1;
                func(vm, code+code_pos+4, j);
                vm->in_function = 0;
                code_pos += 5 + j;
            }
                break;
            case o_method_call:
            {
                if (vm->method_stack_pos+1 == vm->method_stack_size)
                    grow_method_stack(vm);

                mval = vm_regs[code[code_pos+2]]->value.method;
                int register_need = mval->reg_count + num_registers;
                int j;

                if (register_need > max_registers) {
                    grow_vm_registers(vm, register_need);
                    /* Don't forget to update local info... */
                    regs_from_main = vm->regs_from_main;
                    vm_regs        = vm->vm_regs;
                    max_registers  = register_need;
                }

                j = code[code_pos+3];
                /* Prepare the registers for what the method wants. Afterward,
                   update num_registers since prep_registers changes it. */
                prep_registers(vm, mval, code+code_pos);
                num_registers = vm->num_registers;

                stack_entry = vm->method_stack[vm->method_stack_pos-1];
                stack_entry->line_num = code[code_pos+1];
                stack_entry->code_pos = code_pos + j + 5;

                vm_regs = vm_regs + stack_entry->regs_used;
                vm->vm_regs = vm_regs;

                stack_entry->return_reg = -(stack_entry->method->reg_count - code[code_pos+4+j]);
                stack_entry = vm->method_stack[vm->method_stack_pos];
                stack_entry->regs_used = mval->reg_count;
                stack_entry->code = mval->code;
                stack_entry->method = mval;
                vm->method_stack_pos++;

                code = mval->code;
                code_pos = 0;
            }
                break;
            case o_unary_not:
                LOAD_CHECKED_REG(lhs_reg, code_pos, 2)

                rhs_reg = vm_regs[code[code_pos+3]];
                rhs_reg->flags &= ~SYM_IS_NIL;
                rhs_reg->value.integer = !(lhs_reg->value.integer);
                code_pos += 4;
                break;
            case o_unary_minus:
                LOAD_CHECKED_REG(lhs_reg, code_pos, 2)

                rhs_reg = vm_regs[code[code_pos+3]];
                rhs_reg->flags &= ~SYM_IS_NIL;
                rhs_reg->value.integer = -(lhs_reg->value.integer);
                code_pos += 4;
                break;
            case o_return_val:
                /* The current method is at -1.
                   This grabs for -2 because we need the register that the
                   caller reserved for the return. */

                stack_entry = vm->method_stack[vm->method_stack_pos-2];
                /* Note: Upon method entry, vm_regs is moved so that 0 is the
                   start of the new method's registers.
                   Because of this, the register return is a -negative- index
                   that goes back into the caller's stack. */

                lhs_reg = vm_regs[stack_entry->return_reg];
                rhs_reg = vm_regs[code[code_pos+2]];

                /* rhs_reg and lhs_reg are both the same type, so only one
                   is_refcounted check is necessary. */
                if (rhs_reg->sig->cls->is_refcounted) {
                    /* However, one or both could be nil. */
                    if ((rhs_reg->flags & SYM_IS_NIL) == 0)
                        rhs_reg->value.generic->refcount++;

                    if ((lhs_reg->flags & SYM_IS_NIL) == 0)
                        lily_deref_unknown_val(lhs_reg->sig, lhs_reg->value);
                }
                COPY_NIL_FLAG(lhs_reg, rhs_reg->flags)
                lhs_reg->value = rhs_reg->value;

                /* DO NOT BREAK HERE.
                   These two do the same thing from here on, so fall through to
                   share code. */
            case o_return_noval:
                vm->method_stack_pos--;
                stack_entry = vm->method_stack[vm->method_stack_pos-1];

                /* This is the method that was just left. These registers are no
                   longer used, so remove them from the total. */
                num_registers -= vm->method_stack[vm->method_stack_pos]->regs_used;
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
                if (rhs_reg->sig->cls->is_refcounted) {
                    /* However, one or both could be nil. */
                    if ((rhs_reg->flags & SYM_IS_NIL) == 0)
                        rhs_reg->value.generic->refcount++;

                    if ((lhs_reg->flags & SYM_IS_NIL) == 0)
                        lily_deref_unknown_val(lhs_reg->sig, lhs_reg->value);
                }
                COPY_NIL_FLAG(lhs_reg, rhs_reg->flags)
                lhs_reg->value = rhs_reg->value;
                code_pos += 4;
                break;
            case o_set_global:
                rhs_reg = vm_regs[code[code_pos+2]];
                lhs_reg = regs_from_main[code[code_pos+3]];

                /* Use the lhs, because it may be a global object. */
                if (lhs_reg->sig->cls->id != SYM_CLASS_OBJECT) {
                    if (lhs_reg->sig->cls->is_refcounted) {
                        /* However, one or both could be nil. */
                        if ((rhs_reg->flags & SYM_IS_NIL) == 0)
                            rhs_reg->value.generic->refcount++;

                        if ((lhs_reg->flags & SYM_IS_NIL) == 0)
                            lily_deref_unknown_val(lhs_reg->sig, lhs_reg->value);
                    }

                    COPY_NIL_FLAG(lhs_reg, rhs_reg->flags)
                    lhs_reg->value = rhs_reg->value;
                }
                else
                    /* The lhs is an object, so do what object assign does. */
                    op_object_assign(vm, lhs_reg, rhs_reg);

                code_pos += 4;
                break;
            case o_return_expected:
            {
                lily_vm_stack_entry *top;
                top = vm->method_stack[vm->method_stack_pos-1];
                top->line_num = top->code[code_pos+1];
                lily_raise(vm->raiser, lily_ErrReturnExpected,
                        "Method %s completed without returning a value.\n",
                        top->method->trace_name);
            }
                break;
            case o_obj_assign:
                rhs_reg = vm_regs[code[code_pos+2]];
                lhs_reg = vm_regs[code[code_pos+3]];

                op_object_assign(vm, lhs_reg, rhs_reg);
                code_pos += 4;
                break;
            case o_intnum_typecast:
                lhs_reg = vm_regs[code[code_pos+3]];
                LOAD_CHECKED_REG(rhs_reg, code_pos, 2)

                /* Guaranteed to work, because rhs is non-nil and emitter has
                   already verified the types. This will also make sure that the
                   nil flag isn't set on lhs. */
                maybe_crossover_assign(lhs_reg, rhs_reg);
                code_pos += 4;
                break;
            case o_subscript:
                op_subscript(vm, code, code_pos);
                code_pos += 5;
                break;
            case o_sub_assign:
                op_sub_assign(vm, code, code_pos);
                code_pos += 5;
                break;
            case o_build_list:
                op_build_list(vm, vm_regs, code+code_pos);
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
            case o_obj_typecast:
                lhs_reg = vm_regs[code[code_pos+3]];
                cast_sig = lhs_reg->sig;

                LOAD_CHECKED_REG(rhs_reg, code_pos, 2)
                if (rhs_reg->value.object->sig == NULL)
                    novalue_error(vm, code_pos, 2);

                /* This works because lily_ensure_unique_sig makes sure that
                   no two signatures describe the same thing. So if it's the
                   same, then they share the same sig pointer. */
                if (cast_sig == rhs_reg->value.object->sig) {
                    if (lhs_reg->sig->cls->is_refcounted) {
                        rhs_reg->value.object->value.generic->refcount++;
                        if ((lhs_reg->flags & SYM_IS_NIL) == 0)
                            lily_deref_unknown_val(lhs_reg->sig, lhs_reg->value);
                        else
                            lhs_reg->flags &= ~SYM_IS_NIL;

                        lhs_reg->value = rhs_reg->value.object->value;
                    }
                    else {
                        lhs_reg->value = rhs_reg->value.object->value;
                        lhs_reg->flags &= ~SYM_IS_NIL;
                    }
                }
                /* Since integer and number can be cast between each other,
                   allow that with object casts as well. */
                else if (maybe_crossover_assign(lhs_reg, rhs_reg) == 0) {
                    lily_vm_stack_entry *top;
                    top = vm->method_stack[vm->method_stack_pos-1];
                    top->line_num = top->code[code_pos+1];

                    lily_raise(vm->raiser, lily_ErrBadCast,
                            "Cannot cast object containing type '%T' to type '%T'.\n",
                            rhs_reg->value.object->sig, lhs_reg->sig);
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
                    loop_reg->flags &= ~SYM_IS_NIL;
                    code_pos += 7;
                }
                else
                    code_pos = code[code_pos+6];

                break;
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

                    step_reg->flags &= ~SYM_IS_NIL;
                }
                else if (step_reg->value.integer == 0) {
                    LOAD_CHECKED_REG(step_reg, code_pos, 5)

                    lily_raise(vm->raiser, lily_ErrBadValue,
                               "for loop step cannot be 0.\n");
                }

                loop_reg->value.integer = lhs_reg->value.integer;
                loop_reg->flags &= ~SYM_IS_NIL;

                code_pos += 7;
                break;
            case o_return_from_vm:
                for (i = max_registers-1;i >= 0;i--) {
                    if (regs_from_main[i]->sig->cls->is_refcounted &&
                        (regs_from_main[i]->flags & SYM_IS_NIL) == 0) {
                        lily_deref_unknown_val(regs_from_main[i]->sig, regs_from_main[i]->value);
                    }

                    lily_free(vm_regs[i]);
                }

                lily_free(vm_regs);
                vm->vm_regs = NULL;
                vm->regs_from_main = NULL;
                vm->num_registers = 0;
                vm->max_registers = 0;

                /* Remember to remove the jump that the vm installed, since it's
                   no longer valid. */
                vm->raiser->jump_pos--;
                return;
        }
    }
}
