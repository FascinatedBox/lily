#include <stddef.h>
#include <string.h>
#include <inttypes.h>

#include "lily_impl.h"
#include "lily_value.h"
#include "lily_opcode.h"
#include "lily_vm.h"
#include "lily_debug.h"
#include "lily_gc.h"
#include "lily_bind.h"

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

/*****************************************************************************/
/* VM setup and teardown                                                     */
/*****************************************************************************/
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
    lily_vm_catch_entry *catch_entry = lily_malloc(sizeof(lily_vm_catch_entry));

    vm->function_stack = lily_malloc(sizeof(lily_vm_stack_entry *) * 4);
    vm->sipkey = lily_malloc(16);
    vm->foreign_code = lily_malloc(sizeof(uint16_t));
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
    vm->building_error = 0;
    vm->prep_var_start = NULL;
    vm->catch_chain = NULL;
    vm->catch_top = NULL;
    vm->symtab = NULL;
    vm->literal_table = NULL;
    vm->literal_count = 0;
    vm->function_table = NULL;
    vm->function_count = 0;
    vm->prep_literal_start = NULL;
    vm->generic_map = NULL;
    vm->generic_map_size = 0;
    vm->resolver_sigs = NULL;
    vm->resolver_sigs_size = 0;

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
        vm->foreign_code == NULL || catch_entry == NULL) {
        lily_free(catch_entry);
        lily_free(stringbuf);
        lily_free(string_data);
        lily_free_vm_state(vm);
        return NULL;
    }

    vm->catch_chain = catch_entry;
    catch_entry->prev = NULL;
    catch_entry->next = NULL;

    stringbuf->data = string_data;
    stringbuf->data_size = 64;
    vm->string_buffer = stringbuf;
    vm->foreign_code[0] = o_return_from_vm;

    return vm;
}

/*  lily_vm_free_registers
    This is called during parser's teardown to destroy all of the registers.
    The last invoke of the vm is saved until symtab can destroy some stuff. */
void lily_vm_free_registers(lily_vm_state *vm)
{
    lily_value **regs_from_main = vm->regs_from_main;
    lily_value *reg;
    int i;
    if (vm->catch_chain != NULL) {
        while (vm->catch_chain->prev)
            vm->catch_chain = vm->catch_chain->prev;

        lily_vm_catch_entry *catch_iter = vm->catch_chain;
        lily_vm_catch_entry *catch_next;
        while (catch_iter) {
            catch_next = catch_iter->next;
            lily_free(catch_iter);
            catch_iter = catch_next;
        }
    }

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

static void invoke_gc(lily_vm_state *);
static void destroy_gc_entries(lily_vm_state *);

void lily_free_vm_state(lily_vm_state *vm)
{
    int i;
    for (i = 0;i < vm->function_stack_size;i++)
        lily_free(vm->function_stack[i]);

    /* vm->num_registers is now 0, so this will sweep everything. */
    invoke_gc(vm);
    destroy_gc_entries(vm);

    if (vm->string_buffer) {
        lily_free(vm->string_buffer->data);
        lily_free(vm->string_buffer);
    }

    lily_free(vm->resolver_sigs);
    lily_free(vm->generic_map);
    lily_free(vm->function_table);
    lily_free(vm->literal_table);
    lily_free(vm->foreign_code);
    lily_free(vm->sipkey);
    lily_free(vm->function_stack);
    lily_free(vm);
}

/******************************************************************************/
/* Garbage collector                                                          */
/******************************************************************************/

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
static void invoke_gc(lily_vm_state *vm)
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

/*  destroy_gc_entries
    This is called after the last gc invoke to destroy whatever gc_entry values
    are still left. This should only be called when tearing down the vm. */
static void destroy_gc_entries(lily_vm_state *vm)
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

/*****************************************************************************/
/* Shared code                                                               */
/*****************************************************************************/

/*  try_add_gc_item

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
static int try_add_gc_item(lily_vm_state *vm, lily_sig *value_sig,
        lily_generic_gc_val *value)
{
    /* The given value is likely not in a register, so run the gc before adding
       the value to an entry. Otherwise, the value will be destroyed if the gc
       runs. */
    if (vm->gc_live_entry_count >= vm->gc_threshold)
        invoke_gc(vm);

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

/*  grow_function_stack
    The vm wants to hold more functions, so grow the stack. */
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

/*  add_catch_entry
    The vm wants to register a new 'try' block. Give it the space needed. */
static void add_catch_entry(lily_vm_state *vm)
{
    lily_vm_catch_entry *new_entry = lily_malloc(sizeof(lily_vm_catch_entry));
    if (new_entry == NULL)
        lily_raise_nomem(vm->raiser);

    vm->catch_chain->next = new_entry;
    new_entry->next = NULL;
    new_entry->prev = vm->catch_chain;
}

/*  maybe_crossover_assign
    This ugly function handles automatically converting integer to double in
    some cases. Give this the axe when there's a proper integer::to_double and
    double::to_integer. This is ugly and special-casey. */
static int maybe_crossover_assign(lily_value *lhs_reg, lily_value *rhs_reg)
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

/*  grow_vm_registers
    The vm is about to do a function call which requires more registers than it
    has. Make space for more registers, then create initial register values.

    This reallocs vm->regs_from_main. If vm->regs_from_main moves, then
    vm->vm_regs and vm->regs_from_main are updated appropriately.

    Registers created are given the signature of integer, marked nil, and given
    a value of zero (to prevent complaints about invalid reads). */
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

/*  resolve_property_sig
    This takes a signature that is or include generics and resolves it
    according to the given instance. It's used by o_new_instance to figure out
    generic properties.
    This is like resolve_sig_by_map, except there's no map (the instance sig
    is used instead). */
static lily_sig *resolve_property_sig(lily_vm_state *vm,
        lily_sig *instance_sig, lily_sig *prop_sig, int resolve_start)
{
    lily_sig *result_sig = NULL;

    if (prop_sig->cls->id == SYM_CLASS_TEMPLATE)
        result_sig = instance_sig->siglist[prop_sig->template_pos];
    else if (prop_sig->cls->template_count == 0)
        /* The original sig could be something like 'tuple[A, integer]'.
           This keeps 'integer' from building a sig. */
        result_sig = prop_sig;
    else {
        /* If it's marked as having templates and it's not the template
           class, then it -has- to have a siglist to process. */
        int sigs_needed = prop_sig->siglist_size;

        if ((resolve_start + sigs_needed) > vm->resolver_sigs_size) {
            lily_sig **new_sigs = lily_realloc(vm->resolver_sigs,
                    sizeof(lily_sig *) *
                    (resolve_start + sigs_needed));

            if (new_sigs == NULL)
                lily_raise_nomem(vm->raiser);

            vm->resolver_sigs = new_sigs;
            vm->resolver_sigs_size = (resolve_start + sigs_needed);
        }

        int i;
        lily_sig *inner_sig;
        for (i = 0;i < prop_sig->siglist_size;i++) {
            inner_sig = prop_sig->siglist[i];
            inner_sig = resolve_property_sig(vm, instance_sig, prop_sig,
                    resolve_start + i);

            vm->resolver_sigs[resolve_start + i] = inner_sig;
        }

        int flags = (prop_sig->flags & SIG_IS_VARARGS);
        result_sig = lily_build_ensure_sig(vm->symtab, prop_sig->cls, flags,
                vm->resolver_sigs, resolve_start, i);
    }

    return result_sig;
}

/*  resolve_sig_by_map

    function f[A](A value => tuple[A, A]) {
        return <[value, value]
    }

    So, in this case there's a var (or storage) with a signature unlike any
    parameter. The parameters given to the function are known, so use them to
    build the signature that's wanted.

    map_sig_count: How many signatures are in vm->generic_map.
                   The sig at vm->generic_map[i] has a resolved version at
                   vm->generic_map[map_sig_count + i].
    to_resolve:    The generic-holding signature to resolve.
    resolve_start: Where to begin storing signatures in resolver_sigs. Callers
                   should always use zero, but this calls itself with adjusted
                   indexes. */
static lily_sig *resolve_sig_by_map(lily_vm_state *vm, int map_sig_count,
        lily_sig *to_resolve, int resolve_start)
{
    int i, j, sigs_needed = to_resolve->siglist_size;

    if ((resolve_start + sigs_needed) > vm->resolver_sigs_size) {
        lily_sig **new_sigs = lily_realloc(vm->resolver_sigs,
                sizeof(lily_sig *) *
                (resolve_start + sigs_needed));

        if (new_sigs == NULL)
            lily_raise_nomem(vm->raiser);

        vm->resolver_sigs = new_sigs;
        vm->resolver_sigs_size = (resolve_start + sigs_needed);
    }

    lily_sig **sig_map = vm->generic_map;

    for (i = 0;i < to_resolve->siglist_size;i++) {
        lily_sig *inner_sig = to_resolve->siglist[i];
        lily_sig *result_sig = NULL;
        for (j = 0;j < map_sig_count;j++) {
            if (sig_map[j] == inner_sig) {
                result_sig = sig_map[map_sig_count + j];
                break;
            }
        }

        if (result_sig == NULL)
            result_sig = resolve_sig_by_map(vm, map_sig_count, inner_sig,
                    resolve_start + i);

        vm->resolver_sigs[resolve_start + i] = result_sig;
    }

    int flags = (to_resolve->flags & SIG_IS_VARARGS);
    lily_sig *ret = lily_build_ensure_sig(vm->symtab, to_resolve->cls, flags,
            vm->resolver_sigs, resolve_start, i);

    return ret;
}

/*  resolve_generic_registers
    This is called after a generic function's registers have been prepared. It
    looks over the function's parameters to determine what the generics have
    been made into.

    fval:           The function to be called.
    args_collected: How many arguments the function got.
    reg_start:      Where the registers for the function start. This is used
                    with args_collected to get type information from args,
                    which is then used to resolve the locals/storages.

    This is only called if there are locals/storages in a generic function. */
static void resolve_generic_registers(lily_vm_state *vm, lily_function_val *fval,
        int args_collected, int reg_start)
{
    lily_sig **sig_map = vm->generic_map;
    if ((fval->generic_count * 2) > vm->generic_map_size) {
        sig_map = lily_realloc(vm->generic_map,
                sizeof(lily_sig *) * (fval->generic_count * 2));
        if (sig_map == NULL)
            lily_raise_nomem(vm->raiser);

        vm->generic_map = sig_map;
        vm->generic_map_size = fval->generic_count * 2;
    }

    lily_value **regs_from_main = vm->regs_from_main;
    int out_index = fval->generic_count;
    int reg_stop = reg_start + args_collected;
    int i, reg_i, info_i, last_map_spot = 0;
    lily_register_info *ri = fval->reg_info;

    for (i = 0;i < (fval->generic_count * 2);i++)
        sig_map[i] = NULL;

    /* Go through the arguments given to the function to see what the generic
       parameters were morphed into. This will help determine what to do for
       the rest of the registers. */
    for (info_i = 0, reg_i = reg_start;
         reg_i < reg_stop;
         reg_i++, info_i++) {
        lily_value *reg = regs_from_main[reg_i];

        for (i = 0;i <= out_index;i++) {
            if (sig_map[i] == NULL) {
                sig_map[i] = ri[info_i].sig;
                sig_map[out_index + i] = reg->sig;
                last_map_spot = i;
                break;
            }
        }
    }

    reg_stop = reg_i + fval->reg_count - (reg_stop - reg_start);

    for (;
         reg_i < reg_stop;
         reg_i++, info_i++) {
        lily_value *reg = regs_from_main[reg_i];
        lily_sig *generic_want_sig = ri[info_i].sig;
        lily_sig *fixed_want_sig = NULL;
        /* Start off by a basic run through the map built earlier. */
        for (i = 0;i < out_index;i++) {
            if (generic_want_sig == sig_map[i]) {
                fixed_want_sig = sig_map[out_index + i];
                break;
            }
        }

        /* If a var is, say, list[A] and there is no list[A] as a parameter,
           then the signature has to be built. This sucks. */
        if (fixed_want_sig == NULL) {
            fixed_want_sig = resolve_sig_by_map(vm, fval->generic_count,
                    generic_want_sig, 0);

            sig_map[last_map_spot + 1] = generic_want_sig;
            sig_map[out_index + last_map_spot] = fixed_want_sig;
            last_map_spot++;
        }

        if (reg->sig->cls->is_refcounted &&
            (reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
            lily_deref_unknown_val(reg);

        reg->flags = VAL_IS_NIL;
        reg->sig = fixed_want_sig;
    }
}

/*  prep_registers
    This prepares the vm's registers for a 'native' function call. This blasts
    values in the registers for the native call while copying the callee's
    values over. For the rest of the registers that the callee needs, the
    registers are just blasted. */
static void prep_registers(lily_vm_state *vm, lily_function_val *fval,
        uint16_t *code)
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

        /* Important! Registers seeds may reference generics, but incoming
           values have known types. */
        set_reg->sig = get_reg->sig;

        /* This will be null if this register doesn't belong to a
           var, or non-null if it's for a local. */
        if ((get_reg->flags & VAL_IS_NIL) == 0)
            set_reg->value = get_reg->value;
        else
            set_reg->value.integer = 0;

        set_reg->flags = get_reg->flags;
    }

    if (fval->generic_count == 0) {
        /* For the rest of the registers, clear whatever value they have. */
        for (;num_registers < register_need;i++, num_registers++) {
            lily_register_info seed = register_seeds[i];

            lily_value *reg = regs_from_main[num_registers];
            if (reg->sig->cls->is_refcounted &&
                (reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                lily_deref_unknown_val(reg);

            /* SET the flags to nil so that VAL_IS_PROTECTED gets blasted away if
               it happens to be set. */
            reg->flags = VAL_IS_NIL;
            reg->sig = seed.sig;
        }
    }
    else if (num_registers < register_need) {
        resolve_generic_registers(vm, fval, i, num_registers - i);
        num_registers = register_need;
    }

    vm->num_registers = num_registers;
}

/*  copy_literals
    This copies the symtab's literals into the vm's literal_table. This table
    is used so that o_get_const can use a spot for the literal, instead of the
    literal's address. */
static void copy_literals(lily_vm_state *vm)
{
    lily_symtab *symtab = vm->symtab;

    if (vm->literal_count == symtab->next_lit_spot)
        return;

    int count = symtab->next_lit_spot;
    lily_literal **literals;

    if (vm->literal_table == NULL)
        literals = lily_malloc(count * sizeof(lily_literal *));
    else
        literals = lily_realloc(vm->literal_table, count *
                sizeof(lily_literal *));

    if (literals == NULL)
        lily_raise_nomem(vm->raiser);

    lily_literal *lit_iter = vm->prep_literal_start;
    if (lit_iter == NULL)
        lit_iter = symtab->lit_start;

    while (1) {
        literals[lit_iter->reg_spot] = lit_iter;
        if (lit_iter->next == NULL)
            break;

        lit_iter = lit_iter->next;
    }

    vm->literal_table = literals;
    vm->literal_count = count;
    vm->prep_literal_start = lit_iter;
}

/*  copy_vars_to_functions
    This takes a working copy of the function table and adds every function
    in a particular var iter to it. A 'need' is dropped every time. When it is
    0, the caller, copy_functions, will stop loading functions. This is an
    attempt at stopping early when possible. */
static void copy_vars_to_functions(lily_var **functions, lily_var *var_iter,
        int *need)
{
    while (var_iter) {
        if (var_iter->flags & VAR_IS_READONLY) {
            functions[var_iter->reg_spot] = var_iter;
            (*need)--;
            if (*need == 0)
                break;
        }

        var_iter = var_iter->next;
    }
}

/*  copy_functions
    Copy built-in and user-declared functions into the table of functions.
    This is tricker than literals, because functions can come from three
    different places:
    1: In scope functions will be somewhere in symtab->var_start. This includes
       print and printfmt.
    2: Out of scope functions will be in symtab->old_function_chain.
    3: Lastly, any functions needing to be copied will be in a the callable
       section of a class. */
static void copy_functions(lily_vm_state *vm)
{
    lily_symtab *symtab = vm->symtab;

    if (vm->function_count == symtab->next_function_spot)
        return;

    int count = symtab->next_function_spot;
    lily_var **functions;

    if (vm->function_table == NULL)
        functions = lily_malloc(count * sizeof(lily_var *));
    else
        functions = lily_realloc(vm->function_table, count *
                sizeof(lily_var *));

    if (functions == NULL)
        lily_raise_nomem(vm->raiser);

    int need = count - vm->function_count;

    copy_vars_to_functions(functions, symtab->var_chain, &need);
    copy_vars_to_functions(functions, symtab->old_function_chain, &need);
    if (need != 0) {
        lily_class *class_iter = symtab->class_chain;
        while (class_iter) {
            copy_vars_to_functions(functions, class_iter->call_start, &need);
            if (need == 0)
                break;

            class_iter = class_iter->next;
        }
    }

    vm->function_table = functions;
    vm->function_count = count;
}

/*  load_vm_regs
    This is a helper for lily_vm_prep that loads a series of vars into the
    registers given.
    This is currently used to load the sys package and __main__, which have
    values while still in the symtab. It may be used for more stuff in the
    future. */
static void load_vm_regs(lily_value **vm_regs, lily_var *iter_var)
{
    while (iter_var) {
        if ((iter_var->flags & (VAL_IS_NIL | VAR_IS_READONLY)) == 0) {
            if (iter_var->sig->cls->is_refcounted)
                iter_var->value.generic->refcount++;

            vm_regs[iter_var->reg_spot]->flags &= ~VAL_IS_NIL;
            vm_regs[iter_var->reg_spot]->value = iter_var->value;
        }

        iter_var = iter_var->next;
    }
}

/*  bind_function_name
    Create a proper lily_value that represent the name of the given function.
    If the function is from a class, the classname is added too. */
static lily_value *bind_function_name(lily_symtab *symtab,
        lily_function_val *fval)
{
    char *class_name = fval->class_name;
    char *separator;
    if (class_name == NULL) {
        class_name = "";
        separator = "";
    }
    else
        separator = "::";

    int buffer_size = strlen(class_name) + strlen(separator) +
            strlen(fval->trace_name);

    char *buffer = lily_malloc(buffer_size + 1);
    if (buffer == NULL)
        return NULL;

    strcpy(buffer, class_name);
    strcat(buffer, separator);
    strcat(buffer, fval->trace_name);

    return lily_bind_string_take_buffer(symtab, buffer);
}

/*  build_traceback
    Create the list[tuple[string, integer]] stack used to represent traceback
    for an exception. Returns the built traceback, or NULL on fallure. */
static lily_value *build_traceback(lily_vm_state *vm, lily_sig *traceback_sig)
{
    lily_symtab *symtab = vm->symtab;
    lily_list_val *lv = lily_malloc(sizeof(lily_list_val));
    if (lv == NULL)
        return NULL;

    lv->elems = lily_malloc(vm->function_stack_pos * sizeof(lily_value *));
    lv->num_values = -1;
    lv->visited = 0;
    lv->refcount = 1;
    lv->gc_entry = NULL;

    if (lv->elems == NULL) {
        lily_deref_list_val(traceback_sig, lv);
        return NULL;
    }

    int i;
    for (i = 0;i < vm->function_stack_pos;i++) {
        lily_vm_stack_entry *stack_entry = vm->function_stack[i];
        lily_value *tuple_holder = lily_malloc(sizeof(lily_value));
        lily_list_val *stack_tuple = lily_malloc(sizeof(lily_list_val));
        lily_value **tuple_values = lily_malloc(2 * sizeof(lily_value *));

        lily_value *func_string = bind_function_name(symtab,
                stack_entry->function);
        lily_value *linenum_integer = lily_bind_integer(symtab,
                stack_entry->line_num);

        if (tuple_holder == NULL || stack_tuple == NULL ||
            tuple_values == NULL || func_string == NULL ||
            linenum_integer == NULL) {
            lily_bind_destroy(func_string);
            lily_bind_destroy(linenum_integer);
            lily_free(stack_tuple);
            lily_free(tuple_values);
            lily_free(tuple_holder);
            lv->num_values = i;
            lily_deref_list_val(traceback_sig, lv);
            return NULL;
        }

        stack_tuple->num_values = 2;
        stack_tuple->visited = 0;
        stack_tuple->refcount = 1;
        stack_tuple->gc_entry = NULL;
        stack_tuple->elems = tuple_values;
        tuple_values[0] = func_string;
        tuple_values[1] = linenum_integer;
        tuple_holder->sig = traceback_sig->siglist[0];
        tuple_holder->value.list = stack_tuple;
        tuple_holder->flags = 0;
        lv->elems[i] = tuple_holder;
        lv->num_values = i + 1;
    }

    lily_value *v = lily_malloc(sizeof(lily_value));
    if (v == NULL) {
        lily_deref_list_val(traceback_sig, lv);
        return NULL;
    }

    v->value.list = lv;
    v->sig = traceback_sig;
    v->flags = 0;

    return v;
}

/*  novalue_error
    Oh no. This is called when some value is nil and there's an attempt to
    access it. This would be fine...except it TRIES to print the name of the
    thing used if it's a var.
    This function will be DESTROYED when optional values arrive. For now,
    pretend it doesn't exist. Lalalalala. */
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
        lily_raise(vm->raiser, lily_ValueError, "%s is nil.\n",
                   err_reg_info.name);
    else
        lily_raise(vm->raiser, lily_ValueError, "Attempt to use nil value.\n");
}

/*  key_error
    This is a helper routine that raises KeyError when there is an attempt
    to read a hash that does not have the given key.
    Note: This is intentionally not called by o_set_item so that assigning to a
          non-existant part of a hash automatically adds that key.

    vm:       The currently running vm.
    code_pos: The start of the opcode, for getting line info.
    key:      The invalid key passed. */
static void key_error(lily_vm_state *vm, int code_pos, lily_value *key)
{
    lily_vm_stack_entry *top = vm->function_stack[vm->function_stack_pos - 1];
    top->line_num = top->code[code_pos + 1];

    lily_msgbuf *msgbuf = vm->raiser->msgbuf;
    int key_cls_id = key->sig->cls->id;

    lily_msgbuf_add(msgbuf, "KeyError: ");
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

    lily_raise_prebuilt(vm->raiser, lily_KeyError);
}

/*  boundary_error
    Raise IndexError for an invalid index. This is here because a lot of things
    call it. */
static void boundary_error(lily_vm_state *vm, int bad_index)
{
    lily_raise(vm->raiser, lily_IndexError,
            "Subscript index %d is out of range.\n", bad_index);
}

/*****************************************************************************/
/* Built-in function implementations                                         */
/*****************************************************************************/

/*  lily_builtin_calltrace
    Implements: function calltrace(=> tuple[string, integer]) */
void lily_builtin_calltrace(lily_vm_state *vm, lily_function_val *self,
        uint16_t *code)
{
    lily_value *result = vm->vm_regs[code[0]];

    lily_value *traceback_val = build_traceback(vm, result->sig);
    if (traceback_val == NULL)
        lily_raise_nomem(vm->raiser);

    lily_assign_value(vm, result, traceback_val);
}

/*  lily_builtin_show
    Implements: function show[A](A value) */
void lily_builtin_show(lily_vm_state *vm, lily_function_val *self,
        uint16_t *code)
{
    lily_value *reg = vm->vm_regs[code[0]];
    lily_show_value(vm, reg);
}

/*  lily_builtin_print
    Implements: function print(string) */
void lily_builtin_print(lily_vm_state *vm, lily_function_val *self,
        uint16_t *code)
{
    lily_value *reg = vm->vm_regs[code[0]];
    lily_impl_puts(vm->data, reg->value.string->string);
}

/*  lily_builtin_printfmt
    Implements: function printfmt(string, any...) */
void lily_builtin_printfmt(lily_vm_state *vm, lily_function_val *self,
        uint16_t *code)
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
                lily_raise(vm->raiser, lily_FormatError,
                        "Not enough args for printfmt.\n");

            save_ch = fmt[i];
            fmt[i] = '\0';
            lily_impl_puts(data, str_start);
            fmt[i] = save_ch;
            i++;

            arg_av = vararg_lv->elems[arg_pos]->value.any;
            if (arg_av->inner_value->flags & VAL_IS_NIL)
                lily_raise(vm->raiser, lily_FormatError,
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

/*****************************************************************************/
/* Hash related functions                                                    */
/*****************************************************************************/

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

    This raises NoMemoryError on failure. */
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

/*****************************************************************************/
/* Opcode implementations                                                    */
/*****************************************************************************/

/*  do_o_any_assign
    This does o_any_assign for the vm. It's also called by lily_assign_value to
    handle assignments to any (yay code reuse)!

    vm:      If lhs_reg is nil, an any will be made that needs a gc entry.
             The entry will be added to the vm's gc entries.
    lhs_reg: The register containing an any to be assigned to. Might be nil.
    rhs_reg: The register providing a value for the any. Might be nil. */
static void do_o_any_assign(lily_vm_state *vm, lily_value *lhs_reg,
        lily_value *rhs_reg)
{
    lily_any_val *lhs_any;
    if (lhs_reg->flags & VAL_IS_NIL) {
        lhs_any = lily_try_new_any_val();
        if (lhs_any == NULL ||
            try_add_gc_item(vm, lhs_reg->sig,
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

/*  do_o_set_item
    This handles A[B] = C, where A is some sort of list/hash/tuple/whatever.
    If a hash is nil, then it's created and the hash entry is put inside.
    Arguments are pulled from the given code at code_pos.

    +2: The list-like thing to assign to.
    +3: The index, which can't be nil.
    +4: The new value. */
static void do_o_set_item(lily_vm_state *vm, uint16_t *code, int code_pos)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *lhs_reg, *index_reg, *rhs_reg;

    lhs_reg = vm_regs[code[code_pos + 2]];
    LOAD_CHECKED_REG(index_reg, code_pos, 3)
    rhs_reg = vm_regs[code[code_pos + 4]];

    if (lhs_reg->sig->cls->id != SYM_CLASS_HASH) {
        lily_list_val *list_val = lhs_reg->value.list;
        int index_int = index_reg->value.integer;

        LOAD_CHECKED_REG(lhs_reg, code_pos, 2)

        if (index_int >= list_val->num_values)
            boundary_error(vm, index_int);

        /* todo: Wraparound would be nice. */
        if (index_int < 0)
            boundary_error(vm, index_int);

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

/*  do_o_get_item
    This handles A = B[C], where B is some list-like thing, C is an index,
    and A is what will receive the value.
    If B is a hash and nil, or does not have the given key, then KeyError is
    raised.
    Arguments are pulled from the given code at code_pos.

    +2: The list-like thing to assign to.
    +3: The index, which can't be nil.
    +4: The new value. */
static void do_o_get_item(lily_vm_state *vm, uint16_t *code, int code_pos)
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
    if (lhs_reg->sig->cls->id != SYM_CLASS_HASH) {
        lily_list_val *list_val = lhs_reg->value.list;
        int index_int = index_reg->value.integer;

        /* Too big! */
        if (index_int >= lhs_reg->value.list->num_values)
            boundary_error(vm, index_int);

        /* todo: Wraparound would be nice. */
        if (index_int < 0)
            boundary_error(vm, index_int);

        lily_assign_value(vm, result_reg, list_val->elems[index_int]);
    }
    else {
        uint64_t siphash;
        lily_hash_elem *hash_elem;

        siphash = lily_calculate_siphash(vm->sipkey, index_reg);
        hash_elem = lily_try_lookup_hash_elem(lhs_reg->value.hash, siphash,
                index_reg);

        /* Give up if the key doesn't exist. */
        if (hash_elem == NULL)
            key_error(vm, code_pos, index_reg);

        lily_assign_value(vm, result_reg, hash_elem->elem_value);
    }
}

/*  do_o_build_hash
    This creates a new hash value. The emitter ensures that the values are of a
    consistent type and are valid keys. Values must be pulled from the code
    given.

    +2: How many values there are. There are half this many entries.
    +3..*: The pairs, given as (key, value)...
    final: The result to assign the new hash to. */
static void do_o_build_hash(lily_vm_state *vm, uint16_t *code, int code_pos)
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
        try_add_gc_item(vm, result->sig,
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

/*  do_o_build_list_tuple
    This implements creating a new list or tuple. Internally, there's so little
    difference between the two that this works for both. The code given has
    code_pos adjusted into it, so values can be pulled as 'code[X]' instead of
    'code[code_pos+X]'.

    Arguments are pulled from the given code as follows:

    +2: The number of values.
    +3..*: The values.
    final: The result to assign the new list to. */
static void do_o_build_list_tuple(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int num_elems = (intptr_t)(code[2]);
    lily_value *result = vm_regs[code[3+num_elems]];

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

    if (lv->elems == NULL ||
        ((result->sig->flags & SIG_MAYBE_CIRCULAR) &&
          try_add_gc_item(vm, result->sig,
                (lily_generic_gc_val *)lv) == 0)) {

        lily_free(lv->elems);
        lily_free(lv);
        lily_raise_nomem(vm->raiser);
    }

    /* The old value can be destroyed, now that the new value has been made. */
    if ((result->flags & VAL_IS_NIL) == 0)
        lily_deref_unknown_val(result);

    /* Put the new list in the register so the gc doesn't try to collect it. */
    result->value.list = lv;
    /* Make sure the gc can collect when there's an error. */
    result->flags = 0;

    int i;
    for (i = 0;i < num_elems;i++) {
        lily_value *rhs_reg = vm_regs[code[3+i]];

        lv->elems[i] = lily_malloc(sizeof(lily_value));
        if (lv->elems[i] == NULL) {
            lv->num_values = i;
            lily_raise_nomem(vm->raiser);
        }
        lv->elems[i]->flags = VAL_IS_NIL;
        /* For lists, the emitter verifies that each input has the same type.
           For tuples, there is no such restriction. This allows one opcode to
           handle building two (very similar) things. */
        lv->elems[i]->sig = rhs_reg->sig;
        lv->elems[i]->value.integer = 0;
        lv->num_values = i + 1;

        lily_assign_value(vm, lv->elems[i], rhs_reg);
    }

    lv->num_values = num_elems;
}

/*  do_o_raise
    Raise a user-defined exception. A proper traceback value is created and
    given to the user's exception before raising it. */
static void do_o_raise(lily_vm_state *vm, lily_value *exception_val)
{
    /* The Exception class has values[0] as the message, values[1] as the
       container for traceback. */
    if (exception_val->flags & VAL_IS_NIL)
        lily_raise(vm->raiser, lily_ValueError,
                "Cannot raise nil exception.\n");

    lily_instance_val *ival = exception_val->value.instance;
    lily_sig *traceback_sig = ival->values[1]->sig;
    lily_value *traceback = build_traceback(vm, traceback_sig);
    if (traceback == NULL)
        lily_raise_nomem(vm->raiser);

    lily_assign_value(vm, ival->values[1], traceback);
    ival->values[1]->value.list->refcount--;
    lily_free(traceback);

    lily_raise_value(vm->raiser, exception_val);
}

/*  do_o_new_instance
    This is the first opcode of any class constructor. This initalizes the
    hidden '(self)' variable for further accesses. */
static void do_o_new_instance(lily_vm_state *vm, uint16_t *code)
{
    int i, total_entries;
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result = vm_regs[code[2]];
    lily_class *instance_class = result->sig->cls;

    total_entries = instance_class->prop_count;

    lily_instance_val *iv = lily_malloc(sizeof(lily_instance_val));
    lily_value **iv_values = lily_malloc(total_entries * sizeof(lily_value *));

    if (iv == NULL || iv_values == NULL) {
        lily_free(iv);
        lily_free(iv_values);
        lily_raise_nomem(vm->raiser);
    }

    iv->num_values = -1;
    iv->refcount = 1;
    iv->values = iv_values;
    iv->gc_entry = NULL;
    iv->visited = 0;
    iv->true_class = result->sig->cls;

    if ((result->flags & VAL_IS_NIL) == 0)
        lily_deref_unknown_val(result);

    result->value.instance = iv;
    result->flags = 0;

    i = 0;

    lily_prop_entry *prop = instance_class->properties;
    for (i = 0;i < total_entries;i++, prop = prop->next) {
        lily_sig *value_sig = prop->sig;
        iv->values[i] = lily_malloc(sizeof(lily_value));
        if (iv->values[i] == NULL) {
            for (;i >= 0;i--)
                lily_free(iv->values[i]);

            lily_raise_nomem(vm->raiser);
        }

        iv->values[i]->flags = VAL_IS_NIL;
        if (value_sig->template_pos ||
            value_sig->cls->id == SYM_CLASS_TEMPLATE) {
            value_sig = resolve_property_sig(vm, result->sig, value_sig, 0);
        }
        iv->values[i]->sig = value_sig;
        iv->values[i]->value.integer = 0;
    }

    iv->num_values = total_entries;
}

/*****************************************************************************/
/* Exception handling                                                        */
/*****************************************************************************/

/*  make_proper_exception_val
    This is called when an exception is NOT raised by 'raise'. It's used to
    create a exception object that holds the traceback and the message from the
    raiser.
    If this function fails due to being out of memory, the resulting
    NoMemoryError will not be catchable. This prevents an out of memory
    situation from making the interpreter loop forever trying to build an
    exception. */
static void make_proper_exception_val(lily_vm_state *vm,
        lily_class *raised_class, lily_value *result)
{
    /* This is how the vm knows to not catch this. */
    vm->building_error = 1;

    lily_instance_val *ival = lily_malloc(sizeof(lily_instance_val));
    if (ival == NULL)
        lily_raise_nomem(vm->raiser);

    ival->values = lily_malloc(2 * sizeof(lily_value *));
    ival->num_values = -1;
    ival->visited = 0;
    ival->refcount = 1;
    ival->gc_entry = NULL;
    ival->true_class = raised_class;
    if (ival->values == NULL) {
        lily_deref_instance_val(result->sig, ival);
        lily_raise_nomem(vm->raiser);
    }

    lily_value *message_val = lily_bind_string(vm->symtab,
            vm->raiser->msgbuf->message);

    if (message_val == NULL) {
        lily_deref_instance_val(result->sig, ival);
        lily_raise_nomem(vm->raiser);
    }
    lily_msgbuf_reset(vm->raiser->msgbuf);
    ival->values[0] = message_val;
    ival->num_values = 1;

    /* This is safe because this function is only called for builtin errors
       which are always direct subclasses of Exception. */
    lily_class *exception_class = raised_class->parent;
    /* Traceback is always the second property of Exception. */
    lily_sig *traceback_sig = exception_class->properties->next->sig;

    lily_value *traceback_val = build_traceback(vm, traceback_sig);
    if (traceback_val == NULL) {
        lily_deref_instance_val(result->sig, ival);
        lily_raise_nomem(vm->raiser);
    }

    ival->values[1] = traceback_val;
    ival->num_values = 2;

    if ((result->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
        lily_deref_instance_val(result->sig, result->value.instance);

    result->value.instance = ival;
    result->flags = 0;

    vm->building_error = 0;
}

/*  maybe_catch_exception
    This is called when the vm has an exception raised, either from an internal
    operation, or by the user. This looks through the exceptions that the vm
    has registered to see if one of them can handle the current exception.

    Note: In the event that the interpreter hits an out of memory error while
          trying to build an exception value, NoMemoryError is raised and the
          NoMemoryError _is not catchable_. This prevents the interpreter from
          infinite looping trying to make an exception.

    On success: An exception value is created, if the user specified that they
                wanted it.
                Control goes to the appropriate 'except' block.
                1 is returned.
    On failure: 0 is returned, which will result in the vm exiting. */
static int maybe_catch_exception(lily_vm_state *vm)
{
    if (vm->catch_top == NULL)
        return 0;

    const char *except_name;
    lily_class *raised_class;

    if (vm->raiser->exception == NULL) {
        except_name = lily_name_for_error(vm->raiser->error_code);
        raised_class = lily_class_by_name(vm->symtab, except_name);
    }
    else {
        lily_value *raise_val = vm->raiser->exception;
        raised_class = raise_val->sig->cls;
        except_name = raised_class->name;
    }
    /* Until user-declared exception classes arrive, raised_class should not
       be NULL since all errors raiseable -should- be covered... */
    lily_vm_catch_entry *catch_iter = vm->catch_top;
    lily_value *catch_reg = NULL;

    lily_value **stack_regs = vm->vm_regs;
    int adjusted_stack, do_unbox, jump_location, match, stack_pos;

    /* If the last call was to a foreign function, then that function did not
       do a proper stack adjustment (or it wouldn't be able to access the
       caller's registers). So don't include that in stack unwinding
       calculations. */
    if (vm->function_stack[vm->function_stack_pos-1]->function->code == NULL) {
        vm->function_stack_pos--;
        adjusted_stack = 1;
    }
    else
        adjusted_stack = 0;

    match = 0;
    stack_pos = vm->function_stack_pos;

    while (catch_iter != NULL) {
        lily_vm_stack_entry *catch_stack = catch_iter->stack_entry;
        uint16_t *stack_code = catch_stack->code;
        /* A try block is done when the next jump is at 0 (because 0 would
           always be going back, which is illogical otherwise). */
        jump_location = catch_stack->code[catch_iter->code_pos];

        if (stack_pos != (catch_iter->entry_depth + 1)) {
            int i;

            /* If the exception is being caught in another scope, then vm_regs
               needs be adjusted backwards so that the matching registers
               accesses will target the right thing.
               -1: Because vm->function_stack_pos is ahead.
               -1: Because the last vm->function_stack_pos entry is not
                   entered, only prepared if/when it gets entered. */
            for (i = stack_pos - 2;
                 i >= catch_iter->entry_depth;
                 i--) {
                stack_regs = stack_regs - vm->function_stack[i]->regs_used;
            }

            stack_pos = catch_iter->entry_depth + 1;
        }

        while (jump_location != 0) {
            /* Instead of the vm hopping around to different o_except blocks,
               this function visits them to find out which (if any) handles
               the current exception.
               +1 is the line number (for debug), +2 is the spot for the next
               jump, and +3 is a register that holds a value to store this
               particular exception. */
            int next_location = stack_code[jump_location + 2];
            catch_reg = stack_regs[stack_code[jump_location + 4]];
            lily_class *catch_class = catch_reg->sig->cls;
            if (catch_class == raised_class ||
                lily_check_right_inherits_or_is(catch_class, raised_class)) {
                /* ...So that execution resumes from within the except block. */
                do_unbox = stack_code[jump_location + 3];
                jump_location += 5;
                match = 1;
                break;
            }

            jump_location = next_location;
        }

        if (match)
            break;

        catch_iter = catch_iter->prev;
    }

    if (match) {
        if (do_unbox) {
            if (vm->raiser->exception == NULL)
                make_proper_exception_val(vm, raised_class, catch_reg);
            else
                lily_assign_value(vm, catch_reg, vm->raiser->exception);
        }

        vm->function_stack_pos = stack_pos;
        vm->vm_regs = stack_regs;
        vm->function_stack[catch_iter->entry_depth]->code_pos = jump_location;
        /* Each try block can only successfully handle one exception, so use
           ->prev to prevent using the same block again. */
        vm->catch_top = catch_iter->prev;
        if (vm->catch_top != NULL)
            vm->catch_chain = vm->catch_top;
        else
            vm->catch_chain = catch_iter;
    }
    else if (adjusted_stack)
        vm->function_stack_pos++;

    return match;
}

/******************************************************************************/
/* Foreign call API                                                           */
/******************************************************************************/

/* This handles calling a foreign function which will call a native one.
   There are always three interesting functions:
   * (-2) The native caller
   * (-1) The foreign callee
   * (0)  The called native
   When a foreign function is entered, the vm does not adjust vm_regs. This is
   intentional, because builtin functions wouldn't be able to get their values
   if the registers were adjusted.
   Consequently, much of the foreign call stuff adjusts for registers used in
   -2 and -1 to put vm regs where the called native starts. */

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
    /* Adjust for the native caller, and the foreign callee. This puts vm_regs
       where the called native func needs them. */
    int regs_adjust = vm->function_stack[vm->function_stack_pos-2]->regs_used +
                      vm->function_stack[vm->function_stack_pos-1]->regs_used;

    /* Make it so the callee's register indexes target the right things. */
    vm->vm_regs += regs_adjust;

    /* The foreign entry added itself to the stack properly, so just add one
       for the native entry. */
    vm->function_stack_pos++;


    lily_vm_execute(vm);


    /* The return done adjusts for the foreign callee, but not for the native
       caller. Do this or the foreign caller will have busted registers. */
    vm->vm_regs -= vm->function_stack[vm->function_stack_pos-2]->regs_used;
}

/*  lily_vm_get_foreign_reg
    Obtain a value, adjusted for the function to be called. 0 is the value of the
    return (if there is one). Otherwise, this may not be useful. */
lily_value *lily_vm_get_foreign_reg(lily_vm_state *vm, int reg_pos)
{
    lily_value **vm_regs = vm->vm_regs;
    int load_start = vm->function_stack[vm->function_stack_pos-2]->regs_used +
                     vm->function_stack[vm->function_stack_pos-1]->regs_used;

    /* Intentionally adjust by -1 so the return register will be at 0. */
    return vm_regs[load_start + reg_pos - 1];
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
    int load_start = vm->function_stack[vm->function_stack_pos-2]->regs_used +
                     vm->function_stack[vm->function_stack_pos-1]->regs_used;

    /* Intentionally adjust by -1 so the first arg starts at index 1. */
    lily_value *reg = vm_regs[load_start + index - 1];
    lily_assign_value(vm, reg, new_value);
}

static void seed_registers(lily_vm_state *vm, lily_function_val *f, int start)
{
    lily_register_info *info = f->reg_info;
    int max = start + f->reg_count;
    int i;

    for (i = 0;start < max;start++,i++) {
        lily_value *reg = vm->regs_from_main[start];

        if (reg->sig->cls->is_refcounted &&
            (reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
            lily_deref_unknown_val(reg);

        reg->sig = info[i].sig;
        reg->value.integer = 0;
        reg->flags = VAL_IS_NIL;
    }
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
    int callee_start;
    /* In normal function calls, the caller is a function that reserves a
       register to get a value back from the callee. Since that is not the
       case here, add one more register to get a value in case one is
       needed. */
    if (function_val_return_sig != NULL)
        register_need++;

    callee_start = vm->num_registers + (function_val_return_sig != NULL);
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

    seed_registers(vm, function_val, callee_start);

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
    foreign_entry->return_reg = -1;

    /* Step 6: Set the second stack entry (the native function). */
    lily_vm_stack_entry *native_entry = vm->function_stack[vm->function_stack_pos];
    native_entry->code = function_val->code;
    native_entry->code_pos = 0;
    native_entry->regs_used = function_val->reg_count;
    native_entry->return_reg = 0;
    native_entry->function = function_val;
    native_entry->line_num = 0;
}

/******************************************************************************/
/* Exported functions                                                         */
/******************************************************************************/

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
    * If 'left' is an any and there is no memory, NoMemoryError is raised.
    * 'left' must have a type set. */
void lily_assign_value(lily_vm_state *vm, lily_value *left, lily_value *right)
{
    lily_class *cls = left->sig->cls;

    if (cls->id == SYM_CLASS_ANY)
        /* Any assignment is...complicated. Have someone else do it. */
        do_o_any_assign(vm, left, right);
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

/*  lily_vm_prep
    This is called before calling lily_vm_execute if code will start at
    __main__. Foreign callers will use lily_vm_foreign_prep before calling the
    lily_vm_execute.

    * Ensure that there are enough registers to run __main__. If new registers
      are allocated, they're set to a proper signature.
    * Load __main__ and the sys package from symtab into registers.
    * Set stack entry 0 (__main__'s entry). */
void lily_vm_prep(lily_vm_state *vm, lily_symtab *symtab)
{
    lily_var *main_var = symtab->main_var;
    lily_function_val *main_function = main_var->value.function;
    int i;
    lily_var *prep_var_start = vm->prep_var_start;
    if (prep_var_start == NULL)
        prep_var_start = symtab->var_chain;

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
    vm->prep_var_start = symtab->var_chain;
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

    /* Copy literals and functions into their respective tables. This must be
       done after the above block, because these two may lily_raise_nomem. */
    copy_literals(vm);
    copy_functions(vm);

    lily_vm_stack_entry *stack_entry = vm->function_stack[0];
    stack_entry->function = main_function;
    stack_entry->code = main_function->code;
    stack_entry->regs_used = main_function->reg_count;
    stack_entry->return_reg = 0;
    stack_entry->code_pos = 0;
    vm->function_stack_pos = 1;
}

/** The mighty VM **/

/*  lily_vm_execute
    This is Lily's vm. It usually processes code from __main__, but this is
    also called from foreign functions to process native ones.
    This tries to shove big chunks of code out into helper functions, keeping
    the important and hot opcodes in here. */
void lily_vm_execute(lily_vm_state *vm)
{
    lily_function_val *f;
    uint16_t *code;
    lily_vm_stack_entry *stack_entry;
    lily_value **regs_from_main;
    lily_value **vm_regs;
    int i, num_registers, max_registers;
    lily_sig *cast_sig;
    lily_var *temp_var;
    register int64_t for_temp;
    register int code_pos;
    register lily_value *lhs_reg, *rhs_reg, *loop_reg, *step_reg;
    register lily_literal *literal_val;
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

            /* If the vm raises an exception trying to build an exception value
               instance, just...stop and give up. */
            if (vm->building_error || maybe_catch_exception(vm) == 0)
                longjmp(vm->raiser->jumps[vm->raiser->jump_pos-2], 1);
            else {
                code = vm->function_stack[vm->function_stack_pos - 1]->code;
                code_pos = vm->function_stack[vm->function_stack_pos - 1]->code_pos;
                /* If an exception is caught outside the function it's raised
                   in, then vm->vm_regs will get adjusted, so make sure to
                   update the local version. */
                vm_regs = vm->vm_regs;
            }
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
                literal_val = vm->literal_table[code[code_pos+2]];
                lhs_reg = vm_regs[code[code_pos+3]];

                if (lhs_reg->sig->cls->is_refcounted &&
                    (lhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                    lily_deref_unknown_val(lhs_reg);

                lhs_reg->value = literal_val->value;
                lhs_reg->flags = VAL_IS_PROTECTED;
                code_pos += 4;
                break;
            case o_get_function:
                rhs_reg = (lily_value *)(vm->function_table[code[code_pos+2]]);
                lhs_reg = vm_regs[code[code_pos+3]];

                if (lhs_reg->sig->cls->is_refcounted &&
                    (lhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                    lily_deref_unknown_val(lhs_reg);

                lhs_reg->value = rhs_reg->value;
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
                    lily_raise(vm->raiser, lily_DivisionByZeroError,
                            "Attempt to divide by zero.\n");
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
                    lily_raise(vm->raiser, lily_DivisionByZeroError,
                            "Attempt to divide by zero.\n");
                else if (rhs_reg->sig->cls->id == SYM_CLASS_DOUBLE &&
                         rhs_reg->value.doubleval == 0)
                    lily_raise(vm->raiser, lily_DivisionByZeroError,
                            "Attempt to divide by zero.\n");

                INTDBL_OP(/)
                break;
            case o_jump_if:
                lhs_reg = vm_regs[code[code_pos+2]];
                {
                    int cls_id, result;

                    if (lhs_reg->sig->cls->id == SYM_CLASS_ANY &&
                        (lhs_reg->flags & VAL_IS_NIL) == 0)
                        lhs_reg = lhs_reg->value.any->inner_value;

                    /* This should be kept in sync with the literal
                       optimization of emitter. */
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
                if (vm->function_stack_pos > 100)
                    lily_raise(vm->raiser, lily_RecursionError,
                            "Function call recursion limit reached.\n");

                if (vm->function_stack_pos+1 == vm->function_stack_size)
                    grow_function_stack(vm);

                if (code[code_pos+2] == 1)
                    lhs_reg = (lily_value *)vm->function_table[code[code_pos+3]];
                else
                    lhs_reg = vm_regs[code[code_pos+3]];

                fval = (lily_function_val *)(lhs_reg->value.function);

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
                lily_assign_value(vm, lhs_reg, rhs_reg);

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

                lily_assign_value(vm, lhs_reg, rhs_reg);
                code_pos += 4;
                break;
            case o_set_global:
                rhs_reg = vm_regs[code[code_pos+2]];
                lhs_reg = regs_from_main[code[code_pos+3]];

                lily_assign_value(vm, lhs_reg, rhs_reg);
                code_pos += 4;
                break;
            case o_return_expected:
            {
                lily_vm_stack_entry *top;
                top = vm->function_stack[vm->function_stack_pos-1];
                top->line_num = top->code[code_pos+1];
                lily_raise(vm->raiser, lily_ReturnExpectedError,
                        "Function %s completed without returning a value.\n",
                        top->function->trace_name);
            }
                break;
            case o_any_assign:
                rhs_reg = vm_regs[code[code_pos+2]];
                lhs_reg = vm_regs[code[code_pos+3]];

                do_o_any_assign(vm, lhs_reg, rhs_reg);
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
                do_o_get_item(vm, code, code_pos);
                code_pos += 5;
                break;
            case o_set_item:
                do_o_set_item(vm, code, code_pos);
                code_pos += 5;
                break;
            case o_build_hash:
                do_o_build_hash(vm, code, code_pos);
                code_pos += code[code_pos+2] + 4;
                break;
            case o_build_list_tuple:
                do_o_build_list_tuple(vm, code+code_pos);
                code_pos += code[code_pos+2] + 4;
                break;
            case o_ref_assign:
                lhs_reg = vm_regs[code[code_pos+3]];
                rhs_reg = vm_regs[code[code_pos+2]];

                lily_assign_value(vm, lhs_reg, rhs_reg);
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

                /* Symtab ensures that two signatures don't define the same
                   thing, so this is okay. */
                if (cast_sig == rhs_reg->sig)
                    lily_assign_value(vm, lhs_reg, rhs_reg);
                /* Since integer and number can be cast between each other,
                   allow that with any casts as well. */
                else if (maybe_crossover_assign(lhs_reg, rhs_reg) == 0) {
                    lily_vm_stack_entry *top;
                    top = vm->function_stack[vm->function_stack_pos-1];
                    top->line_num = top->code[code_pos+1];

                    lily_raise(vm->raiser, lily_BadTypecastError,
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
            case o_push_try:
            {
                if (vm->catch_chain->next == NULL)
                    add_catch_entry(vm);

                lily_vm_catch_entry *catch_entry = vm->catch_chain;
                catch_entry->stack_entry = vm->function_stack[vm->function_stack_pos - 1];
                catch_entry->entry_depth = vm->function_stack_pos - 1;
                catch_entry->code_pos = code_pos + 2;
                vm->catch_top = vm->catch_chain;
                vm->catch_chain = vm->catch_chain->next;
                code_pos += 3;
                break;
            }
            case o_pop_try:
                vm->catch_chain = vm->catch_top;
                vm->catch_top = vm->catch_top->prev;

                code_pos++;
                break;
            case o_raise:
                lhs_reg = vm_regs[code[code_pos+2]];
                do_o_raise(vm, lhs_reg);
                code_pos += 3;
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
            case o_new_instance:
            {
                do_o_new_instance(vm, code+code_pos);
                code_pos += 3;
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

                    lily_raise(vm->raiser, lily_ValueError,
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
