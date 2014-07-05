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
vm_regs[code[code_pos+4]]->flags &= ~VAL_IS_NIL; \
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
vm_regs[code[code_pos+4]]->flags &= ~VAL_IS_NIL; \
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

    vm->method_stack = lily_malloc(sizeof(lily_vm_stack_entry *) * 4);
    vm->err_function = NULL;
    vm->in_function = 0;
    vm->method_stack_pos = 0;
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
    vm->sipkey = sipkey;
    vm->gc_pass = 0;
    vm->prep_id_start = 0;
    vm->prep_var_start = NULL;

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
int maybe_crossover_assign(lily_value *lhs_reg, lily_value *rhs_reg)
{
    int ret = 1;

    if (rhs_reg->sig->cls->id == SYM_CLASS_OBJECT)
        rhs_reg = rhs_reg->value.object->inner_value;

    if (lhs_reg->sig->cls->id == SYM_CLASS_INTEGER &&
        rhs_reg->sig->cls->id == SYM_CLASS_NUMBER)
        lhs_reg->value.integer = (int64_t)(rhs_reg->value.number);
    else if (lhs_reg->sig->cls->id == SYM_CLASS_NUMBER &&
             rhs_reg->sig->cls->id == SYM_CLASS_INTEGER)
        lhs_reg->value.number = (double)(rhs_reg->value.integer);
    else
        ret = 0;

    if (ret)
        lhs_reg->flags &= ~VAL_IS_NIL;

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
    key:      The invalid key passed. */
void no_such_key_error(lily_vm_state *vm, int code_pos, lily_value *key)
{
    lily_vm_stack_entry *top = vm->method_stack[vm->method_stack_pos - 1];
    top->line_num = top->code[code_pos + 1];

    lily_msgbuf *msgbuf = vm->raiser->msgbuf;
    int key_cls_id = key->sig->cls->id;

    lily_msgbuf_add(msgbuf, "ErrNoSuchKey: ");
    if (key_cls_id == SYM_CLASS_INTEGER)
        lily_msgbuf_add_int(msgbuf, key->value.integer);
    else if (key_cls_id == SYM_CLASS_NUMBER)
        lily_msgbuf_add_double(msgbuf, key->value.number);
    else if (key_cls_id == SYM_CLASS_STR) {
        lily_msgbuf_add_char(msgbuf, '\"');
        /* Note: This is fine for now because strings can't contain \0. */
        lily_msgbuf_add(msgbuf, key->value.str->str);
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
    lily_value *reg = vm->vm_regs[code[0]];
    lily_impl_puts(vm->data, reg->value.str->str);
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
    lily_value **vm_regs = vm->vm_regs;
    lily_value *arg;
    lily_raw_value val;
    void *data = vm->data;

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
            lily_impl_puts(data, str_start);
            fmt[i] = save_ch;
            i++;

            arg = vm_regs[code[arg_pos + 1]]->value.object->inner_value;
            cls_id = arg->sig->cls->id;
            val = arg->value;
            is_nil = 0;

            if (fmt[i] == 'i') {
                if (cls_id != SYM_CLASS_INTEGER)
                    return;
                if (is_nil)
                    lily_impl_puts(data, "(nil)");
                else {
                    snprintf(fmtbuf, 63, "%" PRId64, val.integer);
                    lily_impl_puts(data, fmtbuf);
                }
            }
            else if (fmt[i] == 's') {
                if (cls_id != SYM_CLASS_STR)
                    return;
                if (is_nil)
                    lily_impl_puts(data, "(nil)");
                else
                    lily_impl_puts(data, val.str->str);
            }
            else if (fmt[i] == 'n') {
                if (cls_id != SYM_CLASS_NUMBER)
                    return;

                if (is_nil)
                    lily_impl_puts(data, "(nil)");
                else {
                    snprintf(fmtbuf, 63, "%f", val.number);
                    lily_impl_puts(data, fmtbuf);
                }
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
   for anything that needs that ref/deref stuff except for object. */
void op_ref_assign(lily_value *lhs_reg, lily_value *rhs_reg)
{
    if ((lhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
        lily_deref_unknown_val(lhs_reg);

    if ((rhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
        rhs_reg->value.generic->refcount++;

    lhs_reg->flags = rhs_reg->flags;
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
static uint64_t calculate_siphash(char *sipkey, lily_value *key)
{
    int key_cls_id = key->sig->cls->id;
    uint64_t key_hash;

    if (key_cls_id == SYM_CLASS_STR)
        key_hash = siphash24(key->value.str->str, key->value.str->size,
                sipkey);
    else if (key_cls_id == SYM_CLASS_INTEGER)
        key_hash = key->value.integer;
    else if (key_cls_id == SYM_CLASS_NUMBER)
        /* siphash thinks it's sent a pointer (and will try to deref it), so
           send the address. */
        key_hash = siphash24(&(key->value.number), sizeof(double), sipkey);
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
    key:         The key used for doing the search.

    On success: The hash element that was inserted into the hash value is
                returned.
    On failure: NULL is returned. */
static lily_hash_elem *try_lookup_hash_elem(lily_hash_val *hash,
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
            else if (key_cls_id == SYM_CLASS_NUMBER &&
                     iter_value.number == key_value.number)
                ok = 1;
            else if (key_cls_id == SYM_CLASS_STR &&
                    /* strings are immutable, so try a ptr compare first. */
                    ((iter_value.str == key_value.str) ||
                     /* No? Make sure the sizes match, then call for a strcmp.
                        The size check is an easy way to potentially skip a
                        strcmp in case of hash collision. */
                      (iter_value.str->size == key_value.str->size &&
                       strcmp(iter_value.str->str, key_value.str->str) == 0)))
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

/*  op_object_assign
    This is a vm helper for handling an assignment to an object from another
    value that may or may not be an object.
    Since this call only uses two values, those are passed instead of using
    vm_regs and code like some other vm helpers do.
    vm:      If lhs_reg is nil, an object will be made that needs a gc entry.
             The entry will be added to the vm's gc entries.
    lhs_reg: The register containing an object to be assigned to. Might be nil.
    rhs_reg: The register providing a value for the object. Might be nil. */
static void op_object_assign(lily_vm_state *vm, lily_value *lhs_reg,
        lily_value *rhs_reg)
{
    lily_object_val *lhs_obj;
    if (lhs_reg->flags & VAL_IS_NIL) {
        lhs_obj = lily_try_new_object_val();
        if (lhs_obj == NULL ||
            lily_try_add_gc_item(vm, lhs_reg->sig,
                    (lily_generic_gc_val *)lhs_obj) == 0) {

            if (lhs_obj)
                lily_free(lhs_obj->inner_value);

            lily_free(lhs_obj);
            lily_raise_nomem(vm->raiser);
        }

        lhs_reg->value.object = lhs_obj;
        lhs_reg->flags &= ~VAL_IS_NIL;
    }
    else
        lhs_obj = lhs_reg->value.object;

    lily_sig *new_sig;
    lily_raw_value new_value;
    int new_flags;

    if (rhs_reg->sig->cls->id == SYM_CLASS_OBJECT) {
        if ((rhs_reg->flags & VAL_IS_NIL) ||
            (rhs_reg->value.object->inner_value->flags & VAL_IS_NIL)) {

            new_sig = NULL;
            new_value.integer = 0;
            new_flags = VAL_IS_NIL;
        }
        else {
            lily_value *rhs_inner = rhs_reg->value.object->inner_value;

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

    lily_value *lhs_inner = lhs_obj->inner_value;
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
    key_siphash: The calculated siphash of the given key. Use calculate_siphash
                 to get this.
    hash_key:    The key value, used for lookup. This should not be nil.
    hash_value:  The new value to associate with the given key. This may or may
                 not be nil.

    This raises ErrNoMem on failure. */
static void update_hash_key_value(lily_vm_state *vm, lily_hash_val *hash,
        uint64_t key_siphash, lily_value *hash_key,
        lily_value *hash_value)
{
    lily_hash_elem *elem = try_lookup_hash_elem(hash, key_siphash, hash_key);

    if (elem == NULL) {
        elem = lily_try_new_hash_elem();
        if (elem != NULL) {
            elem->elem_key->flags = 0;
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

/*  op_sub_assign
    This handles subscript assignment for both lists and hashes.
    The vm calls this with the code and the current code position. There are
    three arguments to unpack:
    +2 is the list or hash to assign to. If the hash is nil, a hash value is
       created and a value is put inside.
    +3 is the index. This must not be nil. Lists only allow integer indexes,
       while this is a hash key of the correct type.
    +4 is the new value. This allowed to be nil. */
static void op_sub_assign(lily_vm_state *vm, uintptr_t *code, int code_pos)
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
        siphash = calculate_siphash(vm->sipkey, index_reg);

        update_hash_key_value(vm, lhs_reg->value.hash, siphash, index_reg,
                rhs_reg);
    }
}

/*  op_sub_assign
    This handles subscripting elements from lists and hashes.
    The vm calls this with the code and the current code position. There are
    three arguments to unpack:
    +2 is the list or hash to take a value from.
    +3 is the index to lookup. For hashes, ErrNoSuchKey if the key is not in
       the hash.
    +4 is the new value. This allowed to be nil.

    Note: If the hash is nil, then ErrNoValue is raised instead of
          ErrNoSuchKey. */
static void op_subscript(lily_vm_state *vm, uintptr_t *code, int code_pos)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *lhs_reg, *index_reg, *result_reg;

    /* The lhs is checked, unlike subscript assign. The reason for this is
       because not finding a key results in ErrNoSuchKey. So creating a hash
       where none exists would be useless, because the key that this wants
       is not going to be in an empty hash. */
    LOAD_CHECKED_REG(lhs_reg, code_pos, 2)
    LOAD_CHECKED_REG(index_reg, code_pos, 3)
    result_reg = vm_regs[code[code_pos + 4]];

    if (lhs_reg->sig->cls->id == SYM_CLASS_LIST) {
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

        siphash = calculate_siphash(vm->sipkey, index_reg);
        hash_elem = try_lookup_hash_elem(lhs_reg->value.hash,
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
        key_siphash = calculate_siphash(vm->sipkey, key_reg);

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
    /* This is important for list[object], because it prevents the gc from
       collecting all objects if it's triggered from within the list build. */
    result->flags = 0;

    /* List deref expects that num_elems elements are all allocated.
       Unfortunately, this means having to allocate during each loop. */
    if (elem_sig->cls->id == SYM_CLASS_OBJECT) {
        for (j = 0;j < num_elems;j++) {
            lv->elems[j] = lily_malloc(sizeof(lily_value));
            if (lv->elems[j] == NULL) {
                lv->num_values = j;
                lily_raise_nomem(vm->raiser);
            }

            /* Fix the element in case the next attempt to grab an object
               triggers the gc. */
            lily_value *new_elem = lv->elems[j];
            new_elem->sig = elem_sig;
            new_elem->flags = VAL_IS_NIL;
            new_elem->value.integer = 0;
            lv->num_values = j + 1;
            lily_value *rhs_reg = vm_regs[code[3+j]];

            if ((rhs_reg->flags & VAL_IS_NIL) == 0) {
                lily_value *rhs_inner_val;
                rhs_inner_val = rhs_reg->value.object->inner_value;
                /* Objects are supposed to act like containers which can hold
                   any value. Because of this, the inner value of the other
                   object must be copied over.
                   Objects are also potentially circular, which means each one
                   needs a gc entry. This also means that the list holding the
                   objects has a gc entry. */
                lily_object_val *oval = lily_try_new_object_val();
                if (oval == NULL ||
                    lily_try_add_gc_item(vm, elem_sig,
                            (lily_generic_gc_val *)oval) == 0) {
                    /* Make sure to free the object value made. If it wasn't
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

                new_elem->value.object = oval;
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

static void do_keyword_show(lily_vm_state *vm, int is_global, int reg_id)
{
    lily_value *reg;
    lily_method_val *lily_main;
    lily_method_val *current_method;

    if (is_global)
        reg = vm->regs_from_main[reg_id];
    else
        reg = vm->vm_regs[reg_id];

    lily_main = vm->method_stack[0]->method;
    current_method = vm->method_stack[vm->method_stack_pos - 1]->method;
    lily_show_sym(lily_main, current_method, reg, is_global, reg_id,
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
   This sets up the registers for a method call. This is called after
   grow_registers, and thus assumes that the right number of registers are
   available.
   This also handles copying args for methods. */
static void prep_registers(lily_vm_state *vm, lily_method_val *mval,
        uintptr_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value **regs_from_main = vm->regs_from_main;
    lily_register_info *register_seeds = mval->reg_info;
    int num_registers = vm->num_registers;
    int register_need = vm->num_registers + mval->reg_count;
    int i;

    /* A method's args always come first, so copy arguments over while clearing
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
        lily_register_info seed = mval->reg_info[i];

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
    lily_method_val *main_method = main_var->value.method;
    int i;
    lily_var *prep_var_start = vm->prep_var_start;
    if (prep_var_start == NULL)
        prep_var_start = main_var;

    lily_value **vm_regs;
    if (vm->num_registers > main_method->reg_count)
        vm_regs = vm->vm_regs;
    else {
        /* Note: num_registers can never be zero for a second pass, because the
           first pass will have at least __main__ and the sys package even if
           there's no code to run. */
        if (vm->num_registers == 0)
            vm_regs = lily_malloc(main_method->reg_count *
                    sizeof(lily_value *));
        else
            vm_regs = lily_realloc(vm->regs_from_main,
                    main_method->reg_count * sizeof(lily_value *));

        if (vm_regs == NULL)
            lily_raise_nomem(vm->raiser);

        vm->vm_regs = vm_regs;
    }

    vm->regs_from_main = vm_regs;

    /* Do a special pass to make sure there are values. This allows the second
       loop to just worry about initializing the registers. */
    if (main_method->reg_count > vm->num_registers) {
        for (i = vm->max_registers;i < main_method->reg_count;i++) {
            lily_value *reg = lily_malloc(sizeof(lily_value));
            vm_regs[i] = reg;
            if (reg == NULL) {
                for (;i >= vm->max_registers;i--)
                    lily_free(vm_regs[i]);

                lily_raise_nomem(vm->raiser);
            }
        }
    }

    for (i = vm->prep_id_start;i < main_method->reg_count;i++) {
        lily_value *reg = vm_regs[i];
        lily_register_info seed = main_method->reg_info[i];

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

    if (main_method->reg_count > vm->num_registers) {
        if (vm->num_registers == 0) {
            lily_class *integer_cls = lily_class_by_id(symtab, SYM_CLASS_INTEGER);
            vm->integer_sig = integer_cls->sig;
            vm->main = main_var;
        }

        vm->num_registers = main_method->reg_count;
        vm->max_registers = main_method->reg_count;
    }

    lily_vm_stack_entry *stack_entry = vm->method_stack[0];
    stack_entry->method = main_method;
    stack_entry->code = main_method->code;
    stack_entry->regs_used = main_method->reg_count;
    vm->method_stack_pos = 1;
}

/*  lily_assign_value
    This is an extremely handy function that assigns 'left' to 'right'. This is
    handy because it will handle any refs/derefs needed, nil, and object
    copying.

    vm:    The vm holding the two values. This is needed because if 'left' is
           an object, then a gc pass may be triggered.
    left:  The value to assign to.
           The type of left determines what assignment is used. This is
           important because it means that left must have a type set.
    right: The value to assign.

    Caveats:
    * May raise a nomem error if left is an object and it cannot allocate a
      value to copy right's value.
    * May trigger the vm if if needs to make a new object.
    * Will crash if left does not have a type set. */
void lily_assign_value(lily_vm_state *vm, lily_value *left, lily_value *right)
{
    lily_class *cls = left->sig->cls;

    if (cls->id == SYM_CLASS_OBJECT)
        /* Object assignment is...complicated. Have someone else do it. */
        op_object_assign(vm, left, right);
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
    lily_value **regs_from_main;
    lily_value **vm_regs;
    int i, num_registers, max_registers;
    lily_sig *cast_sig;
    lily_var *temp_var;
    register int64_t for_temp;
    register int code_pos;
    register lily_value *lhs_reg, *rhs_reg, *loop_reg, *step_reg;
    register lily_sym *readonly_sym;
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

                    if (lhs_reg->sig->cls->id == SYM_CLASS_OBJECT &&
                        (lhs_reg->flags & VAL_IS_NIL) == 0)
                        lhs_reg = lhs_reg->value.object->inner_value;

                    if ((lhs_reg->flags & VAL_IS_NIL) == 0) {
                        cls_id = lhs_reg->sig->cls->id;
                        if (cls_id == SYM_CLASS_INTEGER)
                            result = (lhs_reg->value.integer == 0);
                        else if (cls_id == SYM_CLASS_NUMBER)
                            result = (lhs_reg->value.number == 0);
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
                lily_function_val *fval;
                lily_func func;
                int j = code[code_pos+4];

                if (code[code_pos+2] == 1)
                    fval = ((lily_var *)(code[code_pos+3]))->value.function;
                else {
                    lhs_reg = vm_regs[code[code_pos+3]];
                    fval = (lily_function_val *)(lhs_reg->value.function);
                }

                func = fval->func;

                vm->in_function = 1;
                func(vm, code+code_pos+5, j);
                vm->in_function = 0;
                code_pos += 6 + j;
            }
                break;
            case o_method_call:
            {
                if (vm->method_stack_pos+1 == vm->method_stack_size)
                    grow_method_stack(vm);

                if (code[code_pos+2] == 1)
                    mval = ((lily_var *)(code[code_pos+3]))->value.method;
                else {
                    lhs_reg = vm_regs[code[code_pos+3]];
                    mval = (lily_method_val *)(lhs_reg->value.method);
                }

                int register_need = mval->reg_count + num_registers;
                int j;

                if (register_need > max_registers) {
                    grow_vm_registers(vm, register_need);
                    /* Don't forget to update local info... */
                    regs_from_main = vm->regs_from_main;
                    vm_regs        = vm->vm_regs;
                    max_registers  = register_need;
                }

                j = code[code_pos+4];
                /* Prepare the registers for what the method wants. Afterward,
                   update num_registers since prep_registers changes it. */
                prep_registers(vm, mval, code+code_pos);
                num_registers = vm->num_registers;

                stack_entry = vm->method_stack[vm->method_stack_pos-1];
                stack_entry->line_num = code[code_pos+1];
                stack_entry->code_pos = code_pos + j + 6;

                vm_regs = vm_regs + stack_entry->regs_used;
                vm->vm_regs = vm_regs;

                stack_entry->return_reg =
                    -(stack_entry->method->reg_count - code[code_pos+5+j]);
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
                    if ((rhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                        rhs_reg->value.generic->refcount++;

                    if ((lhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                        lily_deref_unknown_val(lhs_reg);
                }
                lhs_reg->flags = rhs_reg->flags;
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
                        if ((rhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                            rhs_reg->value.generic->refcount++;

                        if ((lhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                            lily_deref_unknown_val(lhs_reg);
                    }

                    lhs_reg->flags = rhs_reg->flags;
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
            case o_build_hash:
                op_build_hash(vm, code, code_pos);
                code_pos += code[code_pos+2] + 4;
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
                if ((rhs_reg->flags & VAL_IS_NIL) ||
                    (rhs_reg->value.object->inner_value->flags & VAL_IS_NIL))
                    novalue_error(vm, code_pos, 2);

                rhs_reg = rhs_reg->value.object->inner_value;

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
                   allow that with object casts as well. */
                else if (maybe_crossover_assign(lhs_reg, rhs_reg) == 0) {
                    lily_vm_stack_entry *top;
                    top = vm->method_stack[vm->method_stack_pos-1];
                    top->line_num = top->code[code_pos+1];

                    lily_raise(vm->raiser, lily_ErrBadCast,
                            "Cannot cast object containing type '%T' to type '%T'.\n",
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
                /* Remember to remove the jump that the vm installed, since it's
                   no longer valid. */
                vm->raiser->jump_pos--;
                return;
        }
    }
}
