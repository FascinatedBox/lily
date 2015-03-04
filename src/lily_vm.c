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

#define INTEGER_OP(OP) \
lhs_reg = vm_regs[code[code_pos + 2]]; \
rhs_reg = vm_regs[code[code_pos + 3]]; \
vm_regs[code[code_pos+4]]->value.integer = \
lhs_reg->value.integer OP rhs_reg->value.integer; \
vm_regs[code[code_pos+4]]->flags &= ~VAL_IS_NIL; \
code_pos += 5;

#define INTDBL_OP(OP) \
lhs_reg = vm_regs[code[code_pos + 2]]; \
rhs_reg = vm_regs[code[code_pos + 3]]; \
if (lhs_reg->type->cls->id == SYM_CLASS_DOUBLE) { \
    if (rhs_reg->type->cls->id == SYM_CLASS_DOUBLE) \
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
lhs_reg = vm_regs[code[code_pos + 2]]; \
rhs_reg = vm_regs[code[code_pos + 3]]; \
if (lhs_reg->type->cls->id == SYM_CLASS_DOUBLE) { \
    if (rhs_reg->type->cls->id == SYM_CLASS_DOUBLE) \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.doubleval OP rhs_reg->value.doubleval); \
    else \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.doubleval OP rhs_reg->value.integer); \
} \
else if (lhs_reg->type->cls->id == SYM_CLASS_INTEGER) { \
    if (rhs_reg->type->cls->id == SYM_CLASS_INTEGER) \
        vm_regs[code[code_pos+4]]->value.integer =  \
        (lhs_reg->value.integer OP rhs_reg->value.integer); \
    else \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.integer OP rhs_reg->value.doubleval); \
} \
else if (lhs_reg->type->cls->id == SYM_CLASS_STRING) { \
    vm_regs[code[code_pos+4]]->value.integer = \
    strcmp(lhs_reg->value.string->string, \
           rhs_reg->value.string->string) STRINGOP; \
} \
else if (lhs_reg->type == rhs_reg->type) { \
    vm_regs[code[code_pos+4]]->value.integer = \
    compare_values(vm, lhs_reg, rhs_reg) OP 1; \
} \
vm_regs[code[code_pos+4]]->flags &= ~VAL_IS_NIL; \
code_pos += 5;

#define COMPARE_OP(OP, STRINGOP) \
lhs_reg = vm_regs[code[code_pos + 2]]; \
rhs_reg = vm_regs[code[code_pos + 3]]; \
if (lhs_reg->type->cls->id == SYM_CLASS_DOUBLE) { \
    if (rhs_reg->type->cls->id == SYM_CLASS_DOUBLE) \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.doubleval OP rhs_reg->value.doubleval); \
    else \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.doubleval OP rhs_reg->value.integer); \
} \
else if (lhs_reg->type->cls->id == SYM_CLASS_INTEGER) { \
    if (rhs_reg->type->cls->id == SYM_CLASS_INTEGER) \
        vm_regs[code[code_pos+4]]->value.integer =  \
        (lhs_reg->value.integer OP rhs_reg->value.integer); \
    else \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.integer OP rhs_reg->value.doubleval); \
} \
else if (lhs_reg->type->cls->id == SYM_CLASS_STRING) { \
    vm_regs[code[code_pos+4]]->value.integer = \
    strcmp(lhs_reg->value.string->string, \
           rhs_reg->value.string->string) STRINGOP; \
} \
vm_regs[code[code_pos+4]]->flags &= ~VAL_IS_NIL; \
code_pos += 5;

#define malloc_mem(size)             vm->mem_func(NULL, size)
#define realloc_mem(ptr, size)       vm->mem_func(ptr, size)
#define free_mem(ptr)          (void)vm->mem_func(ptr, 0)

/*****************************************************************************/
/* VM setup and teardown                                                     */
/*****************************************************************************/
lily_vm_state *lily_new_vm_state(lily_mem_func mem_func, lily_raiser *raiser,
        void *data)
{
    lily_vm_state *vm = mem_func(NULL, sizeof(lily_vm_state));
    vm->mem_func = mem_func;

    /* todo: This is a terrible, horrible key to use. Make a better one using
             some randomness or...something. Just not this. */
    char sipkey[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
    lily_vm_stringbuf *stringbuf = malloc_mem(sizeof(lily_vm_stringbuf));
    char *string_data = malloc_mem(64);
    lily_vm_catch_entry *catch_entry = malloc_mem(sizeof(lily_vm_catch_entry));

    vm->function_stack = malloc_mem(sizeof(lily_vm_stack_entry *) * 4);
    vm->sipkey = malloc_mem(16);
    vm->foreign_code = malloc_mem(sizeof(uint16_t));
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
    vm->catch_chain = NULL;
    vm->catch_top = NULL;
    vm->symtab = NULL;
    vm->literal_table = NULL;
    vm->literal_count = 0;
    vm->function_table = NULL;
    vm->function_count = 0;
    vm->prep_literal_stop = NULL;

    if (vm->function_stack) {
        int i;
        for (i = 0;i < 4;i++) {
            vm->function_stack[i] = malloc_mem(sizeof(lily_vm_stack_entry));
            vm->function_stack[i]->build_value = NULL;
        }
        vm->function_stack_size = i;
    }
    else
        vm->function_stack_size = 0;

    if (vm->sipkey)
        memcpy(vm->sipkey, sipkey, 16);

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
            free_mem(catch_iter);
            catch_iter = catch_next;
        }
    }

    for (i = vm->max_registers-1;i >= 0;i--) {
        reg = regs_from_main[i];

        if (reg->type->cls->is_refcounted &&
            (reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
            lily_deref_unknown_val(vm->mem_func, reg);

        free_mem(reg);
    }

    /* This keeps the final gc invoke from touching the now-deleted registers.
       It also ensures the last invoke will get everything. */
    vm->num_registers = 0;
    vm->max_registers = 0;

    free_mem(regs_from_main);
}

static void invoke_gc(lily_vm_state *);
static void destroy_gc_entries(lily_vm_state *);

void lily_free_vm_state(lily_vm_state *vm)
{
    int i;
    for (i = 0;i < vm->function_stack_size;i++)
        free_mem(vm->function_stack[i]);

    /* vm->num_registers is now 0, so this will sweep everything. */
    invoke_gc(vm);
    destroy_gc_entries(vm);

    if (vm->string_buffer) {
        free_mem(vm->string_buffer->data);
        free_mem(vm->string_buffer);
    }

    free_mem(vm->function_table);
    free_mem(vm->literal_table);
    free_mem(vm->foreign_code);
    free_mem(vm->sipkey);
    free_mem(vm->function_stack);
    free_mem(vm);
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
        if ((reg->type->flags & TYPE_MAYBE_CIRCULAR) &&
            (reg->flags & VAL_IS_NIL) == 0 &&
             reg->value.gc_generic->gc_entry != NULL) {
            (*reg->type->cls->gc_marker)(pass, reg);
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
            lily_gc_collect_value(vm->mem_func, gc_iter->value_type,
                    gc_iter->value);
        }
    }

    /* num_registers is -1 if the vm is calling this from lily_free_vm_state and
       there are no registers left. */
    if (num_registers != -1) {
        int i;
        /* Stage 3: Check registers not currently in use to see if they hold a
                    value that's going to be collected. If so, then mark the
                    register as nil so that the value will be cleared later. */
        for (i = vm->num_registers;i < vm->max_registers;i++) {
            lily_value *reg = regs_from_main[i];
            if ((reg->type->flags & TYPE_MAYBE_CIRCULAR) &&
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
            free_mem(gc_iter->value.generic);

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

        free_mem(gc_iter);

        gc_iter = gc_temp;
    }

    gc_iter = vm->gc_spare_entries;
    while (gc_iter != NULL) {
        gc_temp = gc_iter->next;

        free_mem(gc_iter);

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

    vm:         This is sent in case the gc needs to be collected. The new
                gc entry is also added to the vm's ->gc_live_entries.
    value_type: The type describing the value given.
    value:      The value to attach a gc_entry to. This can be any lily_value
                that is a superset of lily_generic_gc_val.

    Returns 1 if successful, 0 otherwise. Additionally, the value's ->gc_entry
    is set to the new gc_entry on success. */
static int try_add_gc_item(lily_vm_state *vm, lily_type *value_type,
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
        new_entry = malloc_mem(sizeof(lily_gc_entry));
    }

    new_entry->value_type = value_type;
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
    class_eq_func eq_func = left->type->cls->eq_func;
    int depth = 0;

    int result = eq_func(vm, &depth, left, right);
    return result;
}

/*  do_box_assign
    This does assignment for any/enum class values for lily_assign_value (since
    it's pretty complex).

    vm:      If lhs_reg is nil, an any will be made that needs a gc entry.
             The entry will be added to the vm's gc entries.
    lhs_reg: The register containing an any to be assigned to. Might be nil.
    rhs_reg: The register providing a value for the any. Might be nil. */
static void do_box_assign(lily_vm_state *vm, lily_value *lhs_reg,
        lily_value *rhs_reg)
{
    lily_any_val *lhs_any;
    if (lhs_reg->flags & VAL_IS_NIL) {
        lhs_any = lily_new_any_val(vm->mem_func);
        try_add_gc_item(vm, lhs_reg->type, (lily_generic_gc_val *)lhs_any);

        lhs_reg->value.any = lhs_any;
        lhs_reg->flags &= ~VAL_IS_NIL;
    }
    else
        lhs_any = lhs_reg->value.any;

    lily_type *new_type;
    lily_raw_value new_value;
    int new_flags;

    if (rhs_reg->type == lhs_reg->type) {
        if ((rhs_reg->flags & VAL_IS_NIL) ||
            (rhs_reg->value.any->inner_value->flags & VAL_IS_NIL)) {

            new_type = NULL;
            new_value.integer = 0;
            new_flags = VAL_IS_NIL;
        }
        else {
            lily_value *rhs_inner = rhs_reg->value.any->inner_value;

            new_type = rhs_inner->type;
            new_value = rhs_inner->value;
            new_flags = rhs_inner->flags;
        }
    }
    else {
        new_type = rhs_reg->type;
        new_value = rhs_reg->value;
        new_flags = rhs_reg->flags;
    }

    if ((new_flags & VAL_IS_NIL_OR_PROTECTED) == 0 &&
        new_type->cls->is_refcounted)
        new_value.generic->refcount++;

    lily_value *lhs_inner = lhs_any->inner_value;
    if ((lhs_inner->flags & VAL_IS_NIL_OR_PROTECTED) == 0 &&
        lhs_inner->type->cls->is_refcounted)
        lily_deref_unknown_val(vm->mem_func, lhs_inner);

    lhs_inner->type = new_type;
    lhs_inner->value = new_value;
    lhs_inner->flags = new_flags;
}

/*  grow_function_stack
    The vm wants to hold more functions, so grow the stack. */
static void grow_function_stack(lily_vm_state *vm)
{
    int i;

    vm->function_stack = realloc_mem(vm->function_stack,
            sizeof(lily_vm_stack_entry *) * 2 * vm->function_stack_size);
    vm->function_stack_size *= 2;

    for (i = vm->function_stack_pos+1;i < vm->function_stack_size;i++) {
        vm->function_stack[i] = malloc_mem(sizeof(lily_vm_stack_entry));
        vm->function_stack[i]->build_value = NULL;
    }
}

/*  add_catch_entry
    The vm wants to register a new 'try' block. Give it the space needed. */
static void add_catch_entry(lily_vm_state *vm)
{
    lily_vm_catch_entry *new_entry = malloc_mem(sizeof(lily_vm_catch_entry));

    vm->catch_chain->next = new_entry;
    new_entry->next = NULL;
    new_entry->prev = vm->catch_chain;
}

/*  grow_vm_registers
    The vm is about to do a function call which requires more registers than it
    has. Make space for more registers, then create initial register values.

    This reallocs vm->regs_from_main. If vm->regs_from_main moves, then
    vm->vm_regs and vm->regs_from_main are updated appropriately.

    Registers created are given the type of integer, marked nil, and given
    a value of zero (to prevent complaints about invalid reads). */
static void grow_vm_registers(lily_vm_state *vm, int register_need)
{
    lily_value **new_regs;
    lily_type *integer_type = vm->integer_type;
    int i = vm->max_registers;

    ptrdiff_t reg_offset = vm->vm_regs - vm->regs_from_main;

    /* Remember, use regs_from_main, NOT vm_regs, which is likely adjusted. */
    new_regs = realloc_mem(vm->regs_from_main, register_need *
            sizeof(lily_value));

    /* Realloc can move the pointer, so always recalculate vm_regs again using
       regs_from_main and the offset. */
    vm->regs_from_main = new_regs;
    vm->vm_regs = new_regs + reg_offset;

    /* Start creating new registers. Have them default to an integer type so that
       nothing has to check for a NULL type. Integer is used as the default
       because it is not ref'd. */
    for (;i < register_need;i++) {
        new_regs[i] = malloc_mem(sizeof(lily_value));
        new_regs[i]->type = integer_type;
        new_regs[i]->flags = VAL_IS_NIL;
    }

    vm->max_registers = register_need;
}

/*  resolve_generic_registers
    This is called after a generic function's registers have been prepared. It
    looks over the function's parameters to determine what the generics have
    been made into.

    func:           A value holding a function to be called.
    result_type:     The type that the result of this function call will
                    produce. This is necessary when the emitter's type
                    inference is used to deduce an output type when an input
                    of that type is not given as an argument.
    args_collected: How many arguments the function got.
    reg_start:      Where the registers for the function start. This is used
                    with args_collected to get type information from args,
                    which is then used to resolve the locals/storages.

    This is only called if there are locals/storages in a generic function. */
static void resolve_generic_registers(lily_vm_state *vm, lily_value *func,
        lily_type *result_type, int args_collected, int reg_start)
{
    lily_value **regs_from_main = vm->regs_from_main;
    int generics_needed = func->type->generic_pos;
    int save_ceiling = lily_ts_raise_ceiling(vm->ts, generics_needed);
    int i;
    lily_function_val *fval = func->value.function;

    /* lily_type_stack has a function called lily_ts_check which both checks
       that types are equal AND initializes generics by the first type seen.
       The return type is ignored through here (it's already been verified by
       emitter so it cannot be wrong). */

    lily_register_info *ri = fval->reg_info;
    if (result_type != NULL)
        lily_ts_check(vm->ts, func->type->subtypes[0], result_type);

    /* Since Lily requires that all variables have a starting value, it is
       therefore impossible to have generics WITHIN a function that are not
       somewhere within the parameters. */
    for (i = 0;i < args_collected;i++) {
        lily_type *left_type = ri[i].type;
        lily_type *right_type = regs_from_main[reg_start + i]->type;

        lily_ts_check(vm->ts, left_type, right_type);
    }

    /* All generics now have types. The types of the rest of the registers
       can be calculated as the resolution of whatever the function says it
       really is. */
    int reg_stop = fval->reg_count;
    for (;i < reg_stop;i++) {
        lily_type *new_type = ri[i].type;
        if (new_type->flags & TYPE_IS_UNRESOLVED)
            new_type = lily_ts_resolve(vm->ts, new_type);

        lily_value *reg = regs_from_main[reg_start + i];
        if (reg->type->cls->is_refcounted &&
            (reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
            lily_deref_unknown_val(vm->mem_func, reg);

        reg->flags = VAL_IS_NIL;
        reg->type = new_type;
    }

    lily_ts_lower_ceiling(vm->ts, save_ceiling);
}

/*  prep_registers
    This prepares the vm's registers for a 'native' function call. This blasts
    values in the registers for the native call while copying the callee's
    values over. For the rest of the registers that the callee needs, the
    registers are just blasted. */
static void prep_registers(lily_vm_state *vm, lily_value *func,
        uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value **regs_from_main = vm->regs_from_main;
    lily_function_val *fval = func->value.function;
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
        if (get_reg->type->cls->is_refcounted &&
            ((get_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0))
            get_reg->value.generic->refcount++;

        if (set_reg->type->cls->is_refcounted &&
            ((set_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0))
            lily_deref_unknown_val(vm->mem_func, set_reg);

        /* Important! Registers seeds may reference generics, but incoming
           values have known types. */
        set_reg->type = get_reg->type;

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
            if (reg->type->cls->is_refcounted &&
                (reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                lily_deref_unknown_val(vm->mem_func, reg);

            /* SET the flags to nil so that VAL_IS_PROTECTED gets blasted away if
               it happens to be set. */
            reg->flags = VAL_IS_NIL;
            reg->type = seed.type;
        }
    }
    else if (num_registers < register_need) {
        lily_type *result_type = NULL;

        /* If the function's result is -1, then it doesn't have one. If it
           does, use that to help do type inference.
           This is necessary when the result has generics that aren't specified
           in one of the arguments. */
        if ((int16_t)(code[5 + code[4]]) != -1)
            result_type = vm_regs[code[5 + code[4]]]->type;

        resolve_generic_registers(vm, func, result_type, i,
                num_registers - i);
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
    lily_literal **literals = realloc_mem(vm->literal_table,
            count * sizeof(lily_literal *));

    lily_literal *lit_stop = vm->prep_literal_stop;
    lily_literal *lit_iter = symtab->lit_chain;

    while (1) {
        literals[lit_iter->reg_spot] = lit_iter;
        if (lit_iter->next == lit_stop)
            break;

        lit_iter = lit_iter->next;
    }

    vm->literal_table = literals;
    vm->literal_count = count;
    vm->prep_literal_stop = symtab->lit_chain;
}

/*  copy_vars_to_functions
    This takes a working copy of the function table and adds every function
    in a particular var iter to it. A 'need' is dropped every time. When it is
    0, the caller will stop loading functions. This is an attempt at stopping
    early when possible. */
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

/*  copy_class_vars_to_functions
    This takes the function table and adds the functions within each class in
    the linked list of classes. */
static void copy_class_vars_to_functions(lily_var **functions,
        lily_class *class_iter, int *need)
{
    while (class_iter) {
        copy_vars_to_functions(functions, class_iter->call_chain, need);
        if (*need == 0)
            break;

        class_iter = class_iter->next;
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
    lily_var **functions = realloc_mem(vm->function_table,
            count * sizeof(lily_var *));

    int need = count - vm->function_count;

    copy_vars_to_functions(functions, symtab->var_chain, &need);
    copy_vars_to_functions(functions, symtab->old_function_chain, &need);
    if (need) {
        copy_class_vars_to_functions(functions, symtab->class_chain, &need);
        copy_class_vars_to_functions(functions, symtab->old_class_chain, &need);
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
            if (iter_var->type->cls->is_refcounted)
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
static lily_value *bind_function_name(lily_vm_state *vm, lily_symtab *symtab,
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

    char *buffer = malloc_mem(buffer_size + 1);

    strcpy(buffer, class_name);
    strcat(buffer, separator);
    strcat(buffer, fval->trace_name);

    return lily_bind_string_take_buffer(symtab, buffer);
}

/*  build_traceback
    Create the list[tuple[string, integer]] stack used to represent traceback
    for an exception. Returns the built traceback, or NULL on fallure. */
static lily_value *build_traceback(lily_vm_state *vm, lily_type *traceback_type)
{
    lily_symtab *symtab = vm->symtab;
    lily_list_val *lv = malloc_mem(sizeof(lily_list_val));

    lv->elems = malloc_mem(vm->function_stack_pos * sizeof(lily_value *));
    lv->num_values = -1;
    lv->visited = 0;
    lv->refcount = 1;
    lv->gc_entry = NULL;

    int i;
    for (i = 0;i < vm->function_stack_pos;i++) {
        lily_vm_stack_entry *stack_entry = vm->function_stack[i];
        lily_value *tuple_holder = malloc_mem(sizeof(lily_value));
        lily_list_val *stack_tuple = malloc_mem(sizeof(lily_list_val));
        lily_value **tuple_values = malloc_mem(2 * sizeof(lily_value *));

        lily_value *func_string = bind_function_name(vm, symtab,
                stack_entry->function);
        lily_value *linenum_integer = lily_bind_integer(symtab,
                stack_entry->line_num);

        stack_tuple->num_values = 2;
        stack_tuple->visited = 0;
        stack_tuple->refcount = 1;
        stack_tuple->gc_entry = NULL;
        stack_tuple->elems = tuple_values;
        tuple_values[0] = func_string;
        tuple_values[1] = linenum_integer;
        tuple_holder->type = traceback_type->subtypes[0];
        tuple_holder->value.list = stack_tuple;
        tuple_holder->flags = 0;
        lv->elems[i] = tuple_holder;
        lv->num_values = i + 1;
    }

    lily_value *v = malloc_mem(sizeof(lily_value));

    v->value.list = lv;
    v->type = traceback_type;
    v->flags = 0;

    return v;
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

    lily_raise(vm->raiser, lily_KeyError, "^V\n", key);
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

    lily_value *traceback_val = build_traceback(vm, result->type);

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

            arg = arg_av->inner_value;
            cls_id = arg->type->cls->id;
            val = arg->value;

            if (fmt[i] == 'd') {
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
            else if (fmt[i] == 'f') {
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

/*  lily_lookup_hash_elem
    This attempts to find a hash element by key in the given hash. This will not
    create a new element if it fails.

    hash:        A valid hash, which may or may not have elements.
    key_siphash: The calculated siphash of the given key. Use
                 lily_calculate_siphash to get this.
    key:         The key used for doing the search.

    On success: The hash element that was inserted into the hash value is
                returned.
    On failure: NULL is returned. */
lily_hash_elem *lily_lookup_hash_elem(lily_hash_val *hash,
        uint64_t key_siphash, lily_value *key)
{
    int key_cls_id = key->type->cls->id;

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

    vm:          The vm that the hash is in.
    hash:        A valid hash, which may or may not have elements.
    key_siphash: The calculated siphash of the given key. Use
                 lily_calculate_siphash to get this.
    hash_key:    The key value, used for lookup.
    hash_value:  The new value to associate with the given key. */
static void update_hash_key_value(lily_vm_state *vm, lily_hash_val *hash,
        uint64_t key_siphash, lily_value *hash_key,
        lily_value *hash_value)
{
    lily_hash_elem *elem;
    elem = lily_lookup_hash_elem(hash, key_siphash, hash_key);

    if (elem == NULL) {
        elem = lily_new_hash_elem(vm->mem_func);
        if (elem != NULL) {
            /* It's important to copy over the flags, in case the key is a
               literal and marked VAL_IS_PROTECTED. Doing so keeps hash deref
               from trying to deref the key. */
            elem->elem_key->flags = hash_key->flags;
            elem->elem_key->value = hash_key->value;
            elem->elem_key->type = hash_key->type;
            elem->key_siphash = key_siphash;

            /* lily_assign_value needs a type for the left side. */
            elem->elem_value->type = hash_value->type;

            elem->next = hash->elem_chain;
            hash->elem_chain = elem;

            hash->num_elems++;
        }
    }

    lily_assign_value(vm, elem->elem_value, hash_value);
}

/*****************************************************************************/
/* Opcode implementations                                                    */
/*****************************************************************************/

/*  do_o_set_item
    This handles A[B] = C, where A is some sort of list/hash/tuple/whatever.
    Arguments are pulled from the given code at code_pos.

    +2: The list-like thing to assign to.
    +3: The index.
    +4: The new value. */
static void do_o_set_item(lily_vm_state *vm, uint16_t *code, int code_pos)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *lhs_reg, *index_reg, *rhs_reg;

    lhs_reg = vm_regs[code[code_pos + 2]];
    index_reg = vm_regs[code[code_pos + 3]];
    rhs_reg = vm_regs[code[code_pos + 4]];

    if (lhs_reg->type->cls->id != SYM_CLASS_HASH) {
        lily_list_val *list_val = lhs_reg->value.list;
        int index_int = index_reg->value.integer;

        if (index_int >= list_val->num_values)
            boundary_error(vm, index_int);

        /* todo: Wraparound would be nice. */
        if (index_int < 0)
            boundary_error(vm, index_int);

        lily_assign_value(vm, list_val->elems[index_int], rhs_reg);
    }
    else {
        uint64_t siphash;
        siphash = lily_calculate_siphash(vm->sipkey, index_reg);

        update_hash_key_value(vm, lhs_reg->value.hash, siphash, index_reg,
                rhs_reg);
    }
}

/*  do_o_get_item
    This handles A = B[C], where B is some list-like thing, C is an index,
    and A is what will receive the value.
    If B does not have the given key, then KeyError is raised.
    Arguments are pulled from the given code at code_pos.

    +2: The list-like thing to assign to.
    +3: The index.
    +4: The new value. */
static void do_o_get_item(lily_vm_state *vm, uint16_t *code, int code_pos)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *lhs_reg, *index_reg, *result_reg;

    lhs_reg = vm_regs[code[code_pos + 2]];
    index_reg = vm_regs[code[code_pos + 3]];
    result_reg = vm_regs[code[code_pos + 4]];

    /* list and tuple have the same representation internally. Since list
       stores proper values, lily_assign_value automagically set the type to
       the right thing. */
    if (lhs_reg->type->cls->id != SYM_CLASS_HASH) {
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
        hash_elem = lily_lookup_hash_elem(lhs_reg->value.hash, siphash,
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

    lily_hash_val *hash_val = lily_new_hash_val(vm->mem_func);

    if ((result->type->flags & TYPE_MAYBE_CIRCULAR))
        try_add_gc_item(vm, result->type,
            (lily_generic_gc_val *)hash_val);

    if ((result->flags & VAL_IS_NIL) == 0)
        lily_deref_hash_val(vm->mem_func, result->type, result->value.hash);

    result->value.hash = hash_val;
    result->flags = 0;

    for (i = 0;
         i < num_values;
         i += 2) {
        key_reg = vm_regs[code[code_pos + 3 + i]];
        value_reg = vm_regs[code[code_pos + 3 + i + 1]];

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

    lily_list_val *lv = malloc_mem(sizeof(lily_list_val));

    /* This is set in case the gc looks at this list. This prevents the gc and
       deref calls from touching ->values and ->flags. */
    lv->num_values = -1;
    lv->visited = 0;
    lv->refcount = 1;
    lv->elems = malloc_mem(num_elems * sizeof(lily_value *));
    lv->gc_entry = NULL;

    if ((result->type->flags & TYPE_MAYBE_CIRCULAR))
        try_add_gc_item(vm, result->type, (lily_generic_gc_val *)lv);

    /* The old value can be destroyed, now that the new value has been made. */
    if ((result->flags & VAL_IS_NIL) == 0)
        lily_deref_unknown_val(vm->mem_func, result);

    /* Put the new list in the register so the gc doesn't try to collect it. */
    result->value.list = lv;
    /* Make sure the gc can collect when there's an error. */
    result->flags = 0;

    int i;
    for (i = 0;i < num_elems;i++) {
        lily_value *rhs_reg = vm_regs[code[3+i]];

        lv->elems[i] = malloc_mem(sizeof(lily_value));
        lv->elems[i]->flags = VAL_IS_NIL;
        /* For lists, the emitter verifies that each input has the same type.
           For tuples, there is no such restriction. This allows one opcode to
           handle building two (very similar) things. */
        lv->elems[i]->type = rhs_reg->type;
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

    lily_instance_val *ival = exception_val->value.instance;
    lily_type *traceback_type = ival->values[1]->type;
    lily_value *traceback = build_traceback(vm, traceback_type);

    lily_assign_value(vm, ival->values[1], traceback);
    ival->values[1]->value.list->refcount--;
    free_mem(traceback);

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
    lily_class *instance_class = result->type->cls;

    total_entries = instance_class->prop_count;

    lily_vm_stack_entry *caller_entry =
            vm->function_stack[vm->function_stack_pos - 1];

    /* Check to see if the caller is in the process of building a subclass
       of this value. If that is the case, then use that instance instead of
       building one that will simply be tossed. */
    if (caller_entry->build_value &&
        caller_entry->build_value->true_class->id > instance_class->id) {

        result->value.instance = caller_entry->build_value;

        /* Important! This allows this memory-saving trick to bubble up through
           multiple ::new calls! */
        vm->function_stack[vm->function_stack_pos]->build_value =
                caller_entry->build_value;
        return;
    }

    lily_instance_val *iv = malloc_mem(sizeof(lily_instance_val));
    lily_value **iv_values = malloc_mem(total_entries * sizeof(lily_value *));

    iv->num_values = -1;
    iv->refcount = 1;
    iv->values = iv_values;
    iv->gc_entry = NULL;
    iv->visited = 0;
    iv->true_class = result->type->cls;

    if ((result->flags & VAL_IS_NIL) == 0)
        lily_deref_unknown_val(vm->mem_func, result);

    result->value.instance = iv;
    result->flags = 0;

    i = 0;

    lily_class *prop_class = instance_class;
    lily_prop_entry *prop = instance_class->properties;
    for (i = total_entries - 1;i >= 0;i--, prop = prop->next) {
        /* If the properties of this class run out, then grab more from the
           superclass. This is single-inheritance, so the properties ARE there
           somewhere. */
        while (prop == NULL) {
            prop_class = prop_class->parent;
            prop = prop_class->properties;
        }

        lily_type *value_type = prop->type;
        iv->values[i] = malloc_mem(sizeof(lily_value));
        iv->values[i]->flags = VAL_IS_NIL;
        if (value_type->flags & TYPE_IS_UNRESOLVED)
            value_type = lily_ts_resolve_by_second(vm->ts, result->type,
                    value_type);

        iv->values[i]->type = value_type;
        iv->values[i]->value.integer = 0;
    }

    iv->num_values = total_entries;

    /* This is set so that a superclass ::new can simply pull this instance,
       since this instance will have >= the # of types. */
    vm->function_stack[vm->function_stack_pos]->build_value = iv;
}

/*****************************************************************************/
/* Exception handling                                                        */
/*****************************************************************************/

/*  make_proper_exception_val
    This is called when an exception is NOT raised by 'raise'. It's used to
    create a exception object that holds the traceback and the message from the
    raiser. */
static void make_proper_exception_val(lily_vm_state *vm,
        lily_class *raised_class, lily_value *result)
{
    lily_instance_val *ival = malloc_mem(sizeof(lily_instance_val));

    ival->values = malloc_mem(2 * sizeof(lily_value *));
    ival->num_values = -1;
    ival->visited = 0;
    ival->refcount = 1;
    ival->gc_entry = NULL;
    ival->true_class = raised_class;

    lily_value *message_val = lily_bind_string(vm->symtab,
            vm->raiser->msgbuf->message);

    lily_msgbuf_flush(vm->raiser->msgbuf);
    ival->values[0] = message_val;
    ival->num_values = 1;

    /* This is safe because this function is only called for builtin errors
       which are always direct subclasses of Exception. */
    lily_class *exception_class = raised_class->parent;
    /* Traceback is always the first property of Exception. */
    lily_type *traceback_type = exception_class->properties->type;

    lily_value *traceback_val = build_traceback(vm, traceback_type);

    ival->values[1] = traceback_val;
    ival->num_values = 2;

    if ((result->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
        lily_deref_instance_val(vm->mem_func, result->type,
                result->value.instance);

    result->value.instance = ival;
    result->flags = 0;
}

/*  maybe_catch_exception
    This is called when the vm has an exception raised, either from an internal
    operation, or by the user. This looks through the exceptions that the vm
    has registered to see if one of them can handle the current exception.

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
        except_name = lily_name_for_error(vm->raiser);
        raised_class = lily_class_by_name(vm->symtab, except_name);
    }
    else {
        lily_value *raise_val = vm->raiser->exception;
        raised_class = raise_val->type->cls;
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
            lily_class *catch_class = catch_reg->type->cls;
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

        if (reg->type->cls->is_refcounted &&
            (reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
            lily_deref_unknown_val(vm->mem_func, reg);

        reg->type = info[i].type;
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
    tocall: A value holding a function to call. */
void lily_vm_foreign_prep(lily_vm_state *vm, lily_function_val *caller,
        lily_value *to_call)
{
    /* Step 1: Determine the total register need of this function. */
    int register_need = to_call->value.function->reg_count;
    lily_type *function_val_return_type = to_call->type->subtypes[0];
    lily_function_val *function_val = to_call->value.function;
    int callee_start;
    /* In normal function calls, the caller is a function that reserves a
       register to get a value back from the callee. Since that is not the
       case here, add one more register to get a value in case one is
       needed. */
    if (function_val_return_type != NULL)
        register_need++;

    callee_start = vm->num_registers + (function_val_return_type != NULL);
    register_need += vm->num_registers;

    /* Step 2: If there aren't enough registers, make them. This may fail. */
    if (register_need > vm->max_registers) {
        grow_vm_registers(vm, register_need);
        /* grow_vm_registers doesn't set this because prep_registers often
           comes after it and initializes registers from vm->num_registers to
           vm->max_registers. So...fix this up. */
        vm->num_registers = register_need;
    }

    /* Step 3: If there's a return register, set it to the proper type.
               -2 may seem deep, but it's correct: 0 is the new native callee,
               -1 is foreign callee, and -2 is the native caller. The result
               register will be in the native caller's registers (so that the) */
    int function_vals_used = vm->function_stack[vm->function_stack_pos-2]->regs_used;

    if (function_val_return_type != NULL) {
        lily_value *foreign_reg = vm->vm_regs[function_vals_used];
        /* Set it to the right type, in case something looks at it later. */
        foreign_reg->type = function_val_return_type;
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
    foreign_entry->regs_used = (function_val_return_type != NULL);
    foreign_entry->return_reg = -1;
    foreign_entry->build_value = NULL;

    /* Step 6: Set the second stack entry (the native function). */
    lily_vm_stack_entry *native_entry = vm->function_stack[vm->function_stack_pos];
    native_entry->code = function_val->code;
    native_entry->code_pos = 0;
    native_entry->regs_used = function_val->reg_count;
    native_entry->return_reg = 0;
    native_entry->function = function_val;
    native_entry->line_num = 0;
    native_entry->build_value = NULL;
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
    * 'left' must have a type set. */
void lily_assign_value(lily_vm_state *vm, lily_value *left, lily_value *right)
{
    lily_class *cls = left->type->cls;

    if (cls->flags & CLS_ENUM_CLASS)
        /* any/enum class assignment is...complicated. Use a separate function
           for it. */
        do_box_assign(vm, left, right);
    else {
        if (cls->is_refcounted) {
            if ((right->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                right->value.generic->refcount++;

            if ((left->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                lily_deref_unknown_val(vm->mem_func, left);
        }

        left->value = right->value;
        left->flags = right->flags;
    }
}

/*  lily_calculate_siphash
    Return a siphash based using the given siphash for the given key.

    sipkey:  The vm's sipkey for creating the hash.
    key:     A value to make a hash for.

    The caller must not pass a non-hashable type (such as any). Parser is
    responsible for ensuring that hashes only use valid key types. */
uint64_t lily_calculate_siphash(char *sipkey, lily_value *key)
{
    int key_cls_id = key->type->cls->id;
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
      are allocated, they're set to a proper type.
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
        vm_regs = realloc_mem(vm->regs_from_main,
                main_function->reg_count * sizeof(lily_value *));
        vm->vm_regs = vm_regs;
    }

    vm->regs_from_main = vm_regs;

    /* Do a special pass to make sure there are values. This allows the second
       loop to just worry about initializing the registers. */
    if (main_function->reg_count > vm->num_registers) {
        for (i = vm->max_registers;i < main_function->reg_count;i++) {
            lily_value *reg = malloc_mem(sizeof(lily_value));
            vm_regs[i] = reg;
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
        reg->type = seed.type;
    }

    load_vm_regs(vm_regs, prep_var_start);

    /* Load only the new globals next time. */
    vm->prep_var_start = symtab->var_chain;
    /* Zap only the slots that new globals need next time. */
    vm->prep_id_start = i;

    if (main_function->reg_count > vm->num_registers) {
        if (vm->num_registers == 0) {
            lily_class *integer_cls = lily_class_by_id(symtab, SYM_CLASS_INTEGER);
            vm->integer_type = integer_cls->type;
            vm->main = main_var;
        }

        vm->num_registers = main_function->reg_count;
        vm->max_registers = main_function->reg_count;
    }

    /* Copy literals and functions into their respective tables. */
    copy_literals(vm);
    copy_functions(vm);

    lily_vm_stack_entry *stack_entry = vm->function_stack[0];
    stack_entry->function = main_function;
    stack_entry->code = main_function->code;
    stack_entry->regs_used = main_function->reg_count;
    stack_entry->return_reg = 0;
    stack_entry->code_pos = 0;
    stack_entry->build_value = NULL;
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
    lily_type *cast_type;
    lily_var *temp_var;
    register int64_t for_temp;
    /* This unfortunately has to be volatile because otherwise calltrace() and
       traceback tend to be 'off'. */
    register volatile int code_pos;
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
            if (maybe_catch_exception(vm) == 0)
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
            case o_fast_assign:
                rhs_reg = vm_regs[code[code_pos+2]];
                lhs_reg = vm_regs[code[code_pos+3]];
                lhs_reg->flags = rhs_reg->flags;
                lhs_reg->value = rhs_reg->value;
                code_pos += 4;
                break;
            case o_get_const:
                literal_val = vm->literal_table[code[code_pos+2]];
                lhs_reg = vm_regs[code[code_pos+3]];

                if (lhs_reg->type->cls->is_refcounted &&
                    (lhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                    lily_deref_unknown_val(vm->mem_func, lhs_reg);

                lhs_reg->value = literal_val->value;
                lhs_reg->flags = VAL_IS_PROTECTED;
                code_pos += 4;
                break;
            case o_get_function:
                rhs_reg = (lily_value *)(vm->function_table[code[code_pos+2]]);
                lhs_reg = vm_regs[code[code_pos+3]];

                if (lhs_reg->type->cls->is_refcounted &&
                    (lhs_reg->flags & VAL_IS_NIL_OR_PROTECTED) == 0)
                    lily_deref_unknown_val(vm->mem_func, lhs_reg);

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
                rhs_reg = vm_regs[code[code_pos+3]];
                if (rhs_reg->value.integer == 0)
                    lily_raise(vm->raiser, lily_DivisionByZeroError,
                            "Attempt to divide by zero.\n");
                INTEGER_OP(/)
                break;
            case o_modulo:
                /* x % 0 will do the same thing as x / 0... */
                rhs_reg = vm_regs[code[code_pos+3]];
                if (rhs_reg->value.integer == 0)
                    lily_raise(vm->raiser, lily_DivisionByZeroError,
                            "Attempt to divide by zero.\n");
                INTEGER_OP(%)
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
                rhs_reg = vm_regs[code[code_pos+3]];
                if (rhs_reg->type->cls->id == SYM_CLASS_INTEGER &&
                    rhs_reg->value.integer == 0)
                    lily_raise(vm->raiser, lily_DivisionByZeroError,
                            "Attempt to divide by zero.\n");
                else if (rhs_reg->type->cls->id == SYM_CLASS_DOUBLE &&
                         rhs_reg->value.doubleval == 0)
                    lily_raise(vm->raiser, lily_DivisionByZeroError,
                            "Attempt to divide by zero.\n");

                INTDBL_OP(/)
                break;
            case o_jump_if:
                lhs_reg = vm_regs[code[code_pos+2]];
                {
                    int cls_id = lhs_reg->type->cls->id;
                    int result;

                    if (cls_id == SYM_CLASS_INTEGER)
                        result = (lhs_reg->value.integer == 0);
                    else if (cls_id == SYM_CLASS_DOUBLE)
                        result = (lhs_reg->value.doubleval == 0);
                    else if (cls_id == SYM_CLASS_STRING)
                        result = (lhs_reg->value.string->size == 0);
                    else if (cls_id == SYM_CLASS_LIST)
                        result = (lhs_reg->value.list->num_values == 0);
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
                    prep_registers(vm, lhs_reg, code+code_pos);
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
                    stack_entry->build_value = NULL;
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
                lhs_reg = vm_regs[code[code_pos+2]];

                rhs_reg = vm_regs[code[code_pos+3]];
                rhs_reg->flags &= ~VAL_IS_NIL;
                rhs_reg->value.integer = !(lhs_reg->value.integer);
                code_pos += 4;
                break;
            case o_unary_minus:
                lhs_reg = vm_regs[code[code_pos+2]];

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
                vm->function_stack[vm->function_stack_pos]->build_value = NULL;

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
            case o_assign:
                rhs_reg = vm_regs[code[code_pos+2]];
                lhs_reg = vm_regs[code[code_pos+3]];

                lily_assign_value(vm, lhs_reg, rhs_reg);
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
            case o_any_typecast:
                lhs_reg = vm_regs[code[code_pos+3]];
                cast_type = lhs_reg->type;

                rhs_reg = vm_regs[code[code_pos+2]];
                rhs_reg = rhs_reg->value.any->inner_value;

                /* Symtab ensures that two types don't define the same
                   thing, so this is okay. */
                if (cast_type == rhs_reg->type)
                    lily_assign_value(vm, lhs_reg, rhs_reg);
                else {
                    lily_vm_stack_entry *top;
                    top = vm->function_stack[vm->function_stack_pos-1];
                    top->line_num = top->code[code_pos+1];

                    lily_raise(vm->raiser, lily_BadTypecastError,
                            "Cannot cast any containing type '^T' to type '^T'.\n",
                            rhs_reg->type, lhs_reg->type);
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
            case o_new_instance:
            {
                do_o_new_instance(vm, code+code_pos);
                code_pos += 3;
                break;
            }
            case o_match_dispatch:
            {
                lhs_reg = vm_regs[code[code_pos+2]];
                lily_class *enum_cls, *variant_cls;

                enum_cls = lhs_reg->type->cls;
                variant_cls = lhs_reg->value.any->inner_value->type->cls;
                for (i = 0;i < code[code_pos+3];i++) {
                    if (enum_cls->variant_members[i] == variant_cls)
                        break;
                }

                /* The emitter ensures that the match pattern is exhaustive, so
                   something must have been found. */
                code_pos = code[code_pos + 4 + i];
                break;
            }
            case o_variant_decompose:
            {
                rhs_reg = vm_regs[code[code_pos + 2]];
                /* Variants are actually tuples stored within an 'any' value
                   (which is the enum clas). Tuples are just lists which the
                   emitter allows different values for.
                   This takes the enum class value and pulls the real variant
                   from it. */
                lily_list_val *variant_val = rhs_reg->value.any->inner_value->value.list;

                /* Each variant value gets mapped away to a register. The
                   emitter ensures that the decomposition won't go too far. */
                for (i = 0;i < code[code_pos+3];i++) {
                    lhs_reg = vm_regs[code[code_pos + 4 + i]];
                    lily_assign_value(vm, lhs_reg, variant_val->elems[i]);
                }

                code_pos += 4 + i;
                break;
            }
            case o_for_setup:
                loop_reg = vm_regs[code[code_pos+2]];
                /* lhs_reg is the start, rhs_reg is the stop. */
                step_reg = vm_regs[code[code_pos+5]];
                lhs_reg = vm_regs[code[code_pos+3]];
                rhs_reg = vm_regs[code[code_pos+4]];

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
