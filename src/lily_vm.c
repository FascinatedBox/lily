#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "lily_alloc.h"
#include "lily_impl.h"
#include "lily_value.h"
#include "lily_opcode.h"
#include "lily_vm.h"
#include "lily_debug.h"
#include "lily_bind.h"
#include "lily_parser.h"
#include "lily_seed.h"

#include "lily_cls_any.h"
#include "lily_cls_hash.h"
#include "lily_cls_function.h"

extern uint64_t siphash24(const void *src, unsigned long src_sz, const char key[16]);

#define INTEGER_OP(OP) \
lhs_reg = vm_regs[code[code_pos + 2]]; \
rhs_reg = vm_regs[code[code_pos + 3]]; \
vm_regs[code[code_pos+4]]->value.integer = \
lhs_reg->value.integer OP rhs_reg->value.integer; \
vm_regs[code[code_pos+4]]->flags = VAL_IS_PRIMITIVE; \
code_pos += 5;

#define INTDBL_OP(OP) \
lhs_reg = vm_regs[code[code_pos + 2]]; \
rhs_reg = vm_regs[code[code_pos + 3]]; \
if (lhs_reg->type == double_type) { \
    if (rhs_reg->type == double_type) \
        vm_regs[code[code_pos+4]]->value.doubleval = \
        lhs_reg->value.doubleval OP rhs_reg->value.doubleval; \
    else \
        vm_regs[code[code_pos+4]]->value.doubleval = \
        lhs_reg->value.doubleval OP rhs_reg->value.integer; \
} \
else \
    vm_regs[code[code_pos+4]]->value.doubleval = \
    lhs_reg->value.integer OP rhs_reg->value.doubleval; \
vm_regs[code[code_pos+4]]->flags = VAL_IS_PRIMITIVE; \
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
if (lhs_reg->type == double_type) { \
    if (rhs_reg->type == double_type) \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.doubleval OP rhs_reg->value.doubleval); \
    else \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.doubleval OP rhs_reg->value.integer); \
} \
else if (lhs_reg->type == integer_type) { \
    if (rhs_reg->type == integer_type) \
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
vm_regs[code[code_pos+4]]->flags = VAL_IS_PRIMITIVE; \
code_pos += 5;

#define COMPARE_OP(OP, STRINGOP) \
lhs_reg = vm_regs[code[code_pos + 2]]; \
rhs_reg = vm_regs[code[code_pos + 3]]; \
if (lhs_reg->type == double_type) { \
    if (rhs_reg->type == double_type) \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.doubleval OP rhs_reg->value.doubleval); \
    else \
        vm_regs[code[code_pos+4]]->value.integer = \
        (lhs_reg->value.doubleval OP rhs_reg->value.integer); \
} \
else if (lhs_reg->type == integer_type) { \
    if (rhs_reg->type == integer_type) \
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
vm_regs[code[code_pos+4]]->flags = VAL_IS_PRIMITIVE; \
code_pos += 5;

static void add_call_frame(lily_vm_state *);

/*****************************************************************************/
/* VM setup and teardown                                                     */
/*****************************************************************************/
lily_vm_state *lily_new_vm_state(lily_options *options,
        lily_raiser *raiser)
{
    lily_vm_state *vm = lily_malloc(sizeof(lily_vm_state));
    vm->data = options->data;
    vm->gc_threshold = options->gc_threshold;

    /* todo: This is a terrible, horrible key to use. Make a better one using
             some randomness or...something. Just not this. */
    char sipkey[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
    lily_vm_catch_entry *catch_entry = lily_malloc(sizeof(lily_vm_catch_entry));

    vm->sipkey = lily_malloc(16);
    vm->foreign_code = lily_malloc(sizeof(uint16_t));
    vm->call_depth = 0;
    vm->raiser = raiser;
    vm->vm_regs = NULL;
    vm->regs_from_main = NULL;
    vm->num_registers = 0;
    vm->max_registers = 0;
    vm->gc_live_entries = NULL;
    vm->gc_spare_entries = NULL;
    vm->gc_live_entry_count = 0;
    vm->gc_pass = 0;
    vm->prep_id_start = 0;
    vm->catch_chain = NULL;
    vm->catch_top = NULL;
    vm->symtab = NULL;
    vm->readonly_table = NULL;
    vm->readonly_count = 0;
    vm->call_chain = NULL;

    add_call_frame(vm);

    if (vm->sipkey)
        memcpy(vm->sipkey, sipkey, 16);

    vm->catch_chain = catch_entry;
    catch_entry->prev = NULL;
    catch_entry->next = NULL;

    vm->foreign_code[0] = o_return_from_vm;

    return vm;
}

static void invoke_gc(lily_vm_state *);
static void destroy_gc_entries(lily_vm_state *);

/*  lily_free_vm
    We're done. Clear out all the values inside of the vm. This runs the garbage
    collector while claiming to have no values so that EVERYTHING that was
    inside a register is cleared out. */
void lily_free_vm(lily_vm_state *vm)
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

        lily_deref(reg);

        lily_free(reg);
    }

    /* This keeps the final gc invoke from touching the now-deleted registers.
       It also ensures the last invoke will get everything. */
    vm->num_registers = 0;
    vm->max_registers = 0;

    lily_free(regs_from_main);

    lily_call_frame *frame_iter = vm->call_chain;
    lily_call_frame *frame_next;

    while (frame_iter->prev)
        frame_iter = frame_iter->prev;

    while (frame_iter) {
        frame_next = frame_iter->next;
        lily_free(frame_iter);
        frame_iter = frame_next;
    }

    /* vm->num_registers is now 0, so this will sweep everything. */
    invoke_gc(vm);
    destroy_gc_entries(vm);

    lily_free(vm->readonly_table);
    lily_free(vm->foreign_code);
    lily_free(vm->sipkey);
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
            lily_gc_collect_value(gc_iter->value_type,
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

/*  Add a lily_gc_entry for the given value. This may call the gc into action if
    there are vm->gc_threshold entries in vm->gc_live_entries before the attempt
    to get a new value.

    Take note, the gc may be invoked regardless of what this call returns.

    vm:         This is sent in case the gc needs to be collected. The new
                gc entry is also added to the vm's ->gc_live_entries.
    value_type: The type describing the value given.
    value:      The value to attach a gc_entry to. This can be any lily_value
                that is a superset of lily_generic_gc_val.

    The value's ->gc_entry is set to the new gc_entry on success. */
static void add_gc_item(lily_vm_state *vm, lily_type *value_type,
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
    }

    new_entry->value_type = value_type;
    new_entry->value.gc_generic = value;
    new_entry->last_pass = 0;

    new_entry->next = vm->gc_live_entries;
    vm->gc_live_entries = new_entry;

    /* Attach the gc_entry to the value so the caller doesn't have to. */
    value->gc_entry = new_entry;
    vm->gc_live_entry_count++;
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
    rhs_reg: The register providing a value for the any. */
static void do_box_assign(lily_vm_state *vm, lily_value *lhs_reg,
        lily_value *rhs_reg)
{
    if (rhs_reg->type == lhs_reg->type)
        rhs_reg = rhs_reg->value.any->inner_value;

    if ((rhs_reg->flags & VAL_IS_NOT_DEREFABLE) == 0)
        rhs_reg->value.generic->refcount++;

    lily_any_val *lhs_any;

    if (lhs_reg->flags & VAL_IS_NIL) {
        lhs_any = lily_new_any_val();
        add_gc_item(vm, lhs_reg->type, (lily_generic_gc_val *)lhs_any);

        lhs_reg->value.any = lhs_any;
        lhs_reg->flags = 0;
    }
    else {
        lhs_any = lhs_reg->value.any;
        lily_deref(lhs_any->inner_value);
    }

    *(lhs_any->inner_value) = *rhs_reg;
}

/*  Add a new call frame to vm's call_chain. The new frame becomes the current
    one. This should be called such that vm->call_chain->next is never NULL. */
static void add_call_frame(lily_vm_state *vm)
{
    lily_call_frame *new_frame = lily_malloc(sizeof(lily_call_frame));

    /* This intentionally doesn't set anything but prev and next because the
       caller will have proper values for those. */
    new_frame->prev = vm->call_chain;
    new_frame->next = NULL;

    if (vm->call_chain != NULL)
        vm->call_chain->next = new_frame;

    vm->call_chain = new_frame;
}

/*  add_catch_entry
    The vm wants to register a new 'try' block. Give it the space needed. */
static void add_catch_entry(lily_vm_state *vm)
{
    lily_vm_catch_entry *new_entry = lily_malloc(sizeof(lily_vm_catch_entry));

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

    /* Size is zero only when this is called the first time and no registers
       have been made available. */
    int size = i;
    if (size == 0)
        size = 1;

    do
        size *= 2;
    while (size < register_need);

    /* Remember, use regs_from_main, NOT vm_regs, which is likely adjusted. */
    new_regs = lily_realloc(vm->regs_from_main, size *
            sizeof(lily_value));

    /* Realloc can move the pointer, so always recalculate vm_regs again using
       regs_from_main and the offset. */
    vm->regs_from_main = new_regs;
    vm->vm_regs = new_regs + reg_offset;

    /* Start creating new registers. Have them default to an integer type so that
       nothing has to check for a NULL type. Integer is used as the default
       because it is not ref'd. */
    for (;i < size;i++) {
        new_regs[i] = lily_malloc(sizeof(lily_value));
        new_regs[i]->type = integer_type;
        new_regs[i]->flags = VAL_IS_NIL | VAL_IS_PRIMITIVE;
    }

    vm->max_registers = size;
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
static void resolve_generic_registers(lily_vm_state *vm,
        lily_function_val *fval, int args_collected, int reg_start)
{
    lily_value **regs_from_main = vm->regs_from_main;
    int save_ceiling = lily_ts_raise_ceiling(vm->ts);
    int i;

    /* lily_type_stack has a function called lily_ts_check which both checks
       that types are equal AND initializes generics by the first type seen.
       The return type is ignored through here (it's already been verified by
       emitter so it cannot be wrong). */

    lily_register_info *ri = fval->reg_info;

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

        lily_deref(reg);

        reg->flags = VAL_IS_NIL | (new_type->cls->flags & VAL_IS_PRIMITIVE);
        reg->type = new_type;
    }

    lily_ts_lower_ceiling(vm->ts, save_ceiling);
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
        if ((get_reg->flags & VAL_IS_NOT_DEREFABLE) == 0)
            get_reg->value.generic->refcount++;

        if ((set_reg->flags & VAL_IS_NOT_DEREFABLE) == 0)
            lily_deref(set_reg);

        *set_reg = *get_reg;
    }

    if (fval->has_generics == 0) {
        /* For the rest of the registers, clear whatever value they have. */
        for (;num_registers < register_need;i++, num_registers++) {
            lily_register_info seed = register_seeds[i];

            lily_value *reg = regs_from_main[num_registers];
            lily_deref(reg);

            /* SET the flags to nil so that VAL_IS_PROTECTED gets blasted away if
               it happens to be set. */
            reg->flags = VAL_IS_NIL;
            reg->type = seed.type;
        }
    }
    else if (num_registers < register_need) {
        resolve_generic_registers(vm, fval, i, num_registers - i);
        num_registers = register_need;
    }

    vm->num_registers = num_registers;
}

static void load_ties_into_readonly(lily_tie **readonly, lily_tie *tie,
        int stop)
{
    /* vm has to prep every time there's a closing ?> tag. It's important that
       only new literals are loaded, to keep from wastefully loading stuff that
       is already loaded. */
    while (tie && tie->reg_spot >= stop) {
        readonly[tie->reg_spot] = tie;
        tie = tie->next;
    }
}

/*  setup_readonly_table
    This makes sure that vm's readonly_table is appropriately sized for any
    new readonly values that the symtab may have. It then walks over them to
    load up only the new ones.
    This table is used by o_get_readonly. */
static void setup_readonly_table(lily_vm_state *vm)
{
    lily_symtab *symtab = vm->symtab;

    if (vm->readonly_count == symtab->next_readonly_spot)
        return;

    int count = symtab->next_readonly_spot;
    lily_tie **new_table = lily_realloc(vm->readonly_table,
            count * sizeof(lily_tie *));

    int load_stop = vm->readonly_count;

    load_ties_into_readonly(new_table, symtab->literals, load_stop);
    load_ties_into_readonly(new_table, symtab->function_ties, load_stop);

    vm->readonly_count = symtab->next_readonly_spot;
    vm->readonly_table = new_table;
}

/*  load_foreign_ties
    This function is called by vm prep to load foreign values into global
    registers. These foreign ties are used by packages to associate values to
    vars before the vm has loaded (ex: the argv of sys, or values in the apache
    server package).

    Since non-built-in packages cannot create these ties, and there are going to
    be VERY few of them, free them as they are loaded. It's not worth it to try
    to reclaim them for use by literals/readonly vars. */
static void load_foreign_ties(lily_vm_state *vm)
{
    lily_tie *tie_iter = vm->symtab->foreign_ties;
    lily_tie *tie_next;
    lily_value **regs_from_main = vm->regs_from_main;

    while (tie_iter) {
        /* Don't use lily_assign_value, because that wants to give the tied
           value a ref. That's bad because then it will have two refs (and the
           tie is just for shifting a value over). */
        lily_value *reg_value = regs_from_main[tie_iter->reg_spot];

        reg_value->type = tie_iter->type;
        /* The flags and type have already been set by vm prep. */
        reg_value->value = tie_iter->value;
        reg_value->flags = (tie_iter->type->cls->flags & VAL_IS_PRIMITIVE);

        tie_next = tie_iter->next;
        lily_free(tie_iter);
        tie_iter = tie_next;
    }

    vm->symtab->foreign_ties = NULL;
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

    char *buffer = lily_malloc(buffer_size + 1);

    strcpy(buffer, class_name);
    strcat(buffer, separator);
    strcat(buffer, fval->trace_name);

    return lily_bind_string_take_buffer(symtab, buffer);
}

/*  build_traceback_raw
    This creates a raw list value containing the current traceback. The
    traceback is represented as list[tuple[string, string, integer]].

    traceback_type: The type of 'list[tuple[string, string, integer]]'. This is
                    passed to avoid having to look up that type in here.

    This should be used in situations where there is an already-made value to
    hold the result of this call. For situations where a proper value is needed,
    use build_traceback instead. */
static lily_list_val *build_traceback_raw(lily_vm_state *vm,
        lily_type *traceback_type)
{
    lily_symtab *symtab = vm->symtab;
    lily_list_val *lv = lily_malloc(sizeof(lily_list_val));

    lv->elems = lily_malloc(vm->call_depth * sizeof(lily_value *));
    lv->num_values = -1;
    lv->visited = 0;
    lv->refcount = 1;
    lv->gc_entry = NULL;
    lily_call_frame *frame_iter;

    int i;

    /* The call chain goes from the most recent to least. Work around that by
       allocating elements in reverse order. It's safe to do this because
       nothing in this loop can trigger the gc. */
    for (i = vm->call_depth, frame_iter = vm->call_chain;
         i >= 1;
         i--, frame_iter = frame_iter->prev) {
        lily_value *tuple_holder = lily_malloc(sizeof(lily_value));
        lily_list_val *stack_tuple = lily_malloc(sizeof(lily_list_val));
        lily_value **tuple_values = lily_malloc(3 * sizeof(lily_value *));

        lily_value *path = lily_bind_string(symtab,
                frame_iter->function->import->path);
        lily_value *func_string = bind_function_name(vm, symtab,
                frame_iter->function);
        lily_value *linenum_integer = lily_bind_integer(symtab,
                frame_iter->line_num);

        stack_tuple->num_values = 3;
        stack_tuple->visited = 0;
        stack_tuple->refcount = 1;
        stack_tuple->gc_entry = NULL;
        stack_tuple->elems = tuple_values;
        tuple_values[0] = path;
        tuple_values[1] = func_string;
        tuple_values[2] = linenum_integer;
        tuple_holder->type = traceback_type->subtypes[0];
        tuple_holder->value.list = stack_tuple;
        tuple_holder->flags = 0;
        lv->elems[i - 1] = tuple_holder;
    }

    lv->num_values = vm->call_depth - 1;
    return lv;
}

/*  build_traceback
    This function acts as a wrapper over build_traceback_raw. It returns the
    traceback put into a proper lily_value struct.

    traceback_type: The type of 'list[tuple[string, string, integer]]'. This is
                    passed to avoid having to look up that type in here. */
static lily_value *build_traceback(lily_vm_state *vm, lily_type *traceback_type)
{
    lily_list_val *lv = build_traceback_raw(vm, traceback_type);
    lily_value *v = lily_malloc(sizeof(lily_value));

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
    vm->call_chain->line_num = vm->call_chain->code[code_pos + 1];

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
    Implements: function calltrace(=> list[tuple[string, integer]]) */
void lily_builtin_calltrace(lily_vm_state *vm, lily_function_val *self,
        uint16_t *code)
{
    lily_value *result = vm->vm_regs[code[0]];

    lily_list_val *traceback_val = build_traceback_raw(vm, result->type);

    lily_raw_value v = {.list = traceback_val};
    lily_move_raw_value(vm, result, 0, v);
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

void lily_process_format_string(lily_vm_state *vm, uint16_t *code)
{
    char *fmt;
    int arg_pos, fmt_index;
    lily_list_val *vararg_lv;
    lily_value **vm_regs = vm->vm_regs;
    lily_msgbuf *vm_buffer = vm->vm_buffer;
    lily_msgbuf_flush(vm_buffer);

    fmt = vm_regs[code[0]]->value.string->string;
    vararg_lv = vm_regs[code[1]]->value.list;
    arg_pos = 0;
    fmt_index = 0;
    int text_start = 0;
    int text_stop = 0;

    while (1) {
        char ch = fmt[fmt_index];

        if (ch != '%') {
            if (ch == '\0')
                break;

            text_stop++;
        }
        else if (ch == '%') {
            if (arg_pos == vararg_lv->num_values)
                lily_raise(vm->raiser, lily_FormatError,
                        "Not enough args for printfmt.\n");

            lily_msgbuf_add_text_range(vm_buffer, fmt, text_start, text_stop);
            text_start = fmt_index + 2;
            text_stop = text_start;

            fmt_index++;

            lily_value *arg = vararg_lv->elems[arg_pos]->value.any->inner_value;
            int cls_id = arg->type->cls->id;
            lily_raw_value val = arg->value;

            if (fmt[fmt_index] == 'd') {
                if (cls_id != SYM_CLASS_INTEGER)
                    lily_raise(vm->raiser, lily_FormatError,
                            "%%d is not valid for type ^T.\n",
                            arg->type);

                lily_msgbuf_add_int(vm_buffer, val.integer);
            }
            else if (fmt[fmt_index] == 's') {
                if (cls_id != SYM_CLASS_STRING)
                    lily_raise(vm->raiser, lily_FormatError,
                            "%%s is not valid for type ^T.\n",
                            arg->type);

                lily_msgbuf_add(vm_buffer, val.string->string);
            }
            else if (fmt[fmt_index] == 'f') {
                if (cls_id != SYM_CLASS_DOUBLE)
                    lily_raise(vm->raiser, lily_FormatError,
                            "%%f is not valid for type ^T.\n",
                            arg->type);

                lily_msgbuf_add_double(vm_buffer, val.doubleval);
            }
            arg_pos++;
        }
        fmt_index++;
    }

    lily_msgbuf_add_text_range(vm_buffer, fmt, text_start, text_stop);
}

/*  lily_builtin_printfmt
    Implements: function printfmt(string, any...) */
void lily_builtin_printfmt(lily_vm_state *vm, lily_function_val *self,
        uint16_t *code)
{
    lily_process_format_string(vm, code);
    lily_impl_puts(vm->data, vm->vm_buffer->message);
    lily_msgbuf_flush(vm->vm_buffer);
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
        elem = lily_new_hash_elem();
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

static void do_o_set_property(lily_vm_state *vm, uint16_t *code, int code_pos)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *rhs_reg;
    int index;
    lily_instance_val *ival;

    ival = vm_regs[code[code_pos + 2]]->value.instance;
    index = code[code_pos + 3];
    rhs_reg = vm_regs[code[code_pos + 4]];

    lily_assign_value(vm, ival->values[index], rhs_reg);
}

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

        if (index_int < 0) {
            int new_index = list_val->num_values + index_int;
            if (new_index < 0)
                boundary_error(vm, index_int);

            index_int = new_index;
        }
        else if (index_int >= list_val->num_values)
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

static void do_o_get_property(lily_vm_state *vm, uint16_t *code, int code_pos)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg;
    int index;
    lily_instance_val *ival;

    ival = vm_regs[code[code_pos + 2]]->value.instance;
    index = code[code_pos + 3];
    result_reg = vm_regs[code[code_pos + 4]];

    lily_assign_value(vm, result_reg, ival->values[index]);
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

        if (index_int < 0) {
            int new_index = list_val->num_values + index_int;
            if (new_index < 0)
                boundary_error(vm, index_int);

            index_int = new_index;
        }
        else if (index_int >= list_val->num_values)
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

    lily_hash_val *hash_val = lily_new_hash_val();

    if ((result->type->flags & TYPE_MAYBE_CIRCULAR))
        add_gc_item(vm, result->type, (lily_generic_gc_val *)hash_val);

    lily_deref(result);

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

    lily_list_val *lv = lily_malloc(sizeof(lily_list_val));

    /* This is set in case the gc looks at this list. This prevents the gc and
       deref calls from touching ->values and ->flags. */
    lv->num_values = -1;
    lv->visited = 0;
    lv->refcount = 1;
    lv->elems = lily_malloc(num_elems * sizeof(lily_value *));
    lv->gc_entry = NULL;

    if ((result->type->flags & TYPE_MAYBE_CIRCULAR))
        add_gc_item(vm, result->type, (lily_generic_gc_val *)lv);

    /* The old value can be destroyed, now that the new value has been made. */
    lily_deref(result);

    /* Put the new list in the register so the gc doesn't try to collect it. */
    result->value.list = lv;
    /* Make sure the gc can collect when there's an error. */
    result->flags = 0;

    int i;
    for (i = 0;i < num_elems;i++) {
        lily_value *rhs_reg = vm_regs[code[3+i]];

        lv->elems[i] = lily_malloc(sizeof(lily_value));
        /* For lists, the emitter verifies that each input has the same type.
           For tuples, there is no such restriction. This allows one opcode to
           handle building two (very similar) things. */
        lv->elems[i]->type = rhs_reg->type;
        lv->elems[i]->flags = VAL_IS_NIL;
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
    char *message = ival->values[0]->value.string->string;

    lily_raise_type_and_msg(vm->raiser, exception_val->type, message);
}

/*  do_o_setup_optargs
    This is called when a function takes at least one optional argument. This
    walks backward from the right. Any value that doesn't have something set
    (marked VAL_IS_NIL) is given the corresponding literal value. */
static void do_o_setup_optargs(lily_vm_state *vm, uint16_t *code, int code_pos)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_tie **ro_table = vm->readonly_table;

    /* This goes in reverse because arguments fill from the bottom up. Doing it
       this way means that the loop can stop when it finds the first filled
       argument. It's a slight optimization, but such an easy one. */
    int count = code[code_pos + 1];
    int i = count + code_pos + 1;
    int half = count / 2;
    int end = i - half;

    for (;i > end;i--) {
        lily_value *left = vm_regs[code[i]];
        if ((left->flags & VAL_IS_NIL) == 0)
            break;

        /* Note! The right side is ALWAYS a literal. Do not use vm_regs! */
        lily_tie *right = ro_table[code[i - half]];

        /* The left side has been verified to be nil, so there is no reason for
           a deref check.
           The right side is a literal, so it is definitely marked protected,
           and there is no need to check to ref it. */
        left->flags = right->flags;
        left->value = right->value;
    }
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

    lily_call_frame *caller_frame = vm->call_chain->prev;

    /* Check to see if the caller is in the process of building a subclass
       of this value. If that is the case, then use that instance instead of
       building one that will simply be tossed. */
    if (caller_frame->build_value &&
        caller_frame->build_value->true_class->id > instance_class->id) {

        result->value.instance = caller_frame->build_value;

        /* Important! This allows this memory-saving trick to bubble up through
           multiple ::new calls! */
        vm->call_chain->build_value = caller_frame->build_value;
        return;
    }

    lily_instance_val *iv = lily_malloc(sizeof(lily_instance_val));
    lily_value **iv_values = lily_malloc(total_entries * sizeof(lily_value *));

    iv->num_values = -1;
    iv->refcount = 1;
    iv->values = iv_values;
    iv->gc_entry = NULL;
    iv->visited = 0;
    iv->true_class = result->type->cls;

    if ((result->type->flags & TYPE_MAYBE_CIRCULAR))
        add_gc_item(vm, result->type, (lily_generic_gc_val *)iv);

    lily_deref(result);

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
        iv->values[i] = lily_malloc(sizeof(lily_value));
        iv->values[i]->flags = VAL_IS_NIL;

        if (value_type->flags & TYPE_IS_UNRESOLVED)
            value_type = lily_ts_resolve_by_second(vm->ts, result->type,
                    value_type);

        iv->values[i]->type = value_type;
    }

    iv->num_values = total_entries;

    /* This is set so that a superclass ::new can simply pull this instance,
       since this instance will have >= the # of types. */
    vm->call_chain->build_value = iv;
}

/*****************************************************************************/
/* Closures                                                                  */
/*****************************************************************************/

/*  This function attempts to create the original closure. Other opcodes, such
    as o_create_function will make a shallow copy of the closure data within
    here.
    The upvalues of the closure are not initially provided types. This is
    because the closure cannot have access to all type information when it is
    first invoked (an inner function may define extra generics that a further
    inward function may then use). */
static lily_value **do_o_create_closure(lily_vm_state *vm, uint16_t *code)
{
    int count = code[2];
    lily_value *result = vm->vm_regs[code[3]];

    lily_function_val *last_call = vm->call_chain->function;

    lily_function_val *closure_func = lily_new_function_copy(last_call);
    /* There's probably a way of determining at emit time if a closure -really-
       needs to have a marker at this stage. However, it's easier and safer to
       just assume it needs a marker. */
    add_gc_item(vm, result->type, (lily_generic_gc_val *)closure_func);

    lily_closure_data *d = lily_malloc(sizeof(lily_closure_data));
    lily_value **upvalues = lily_malloc(sizeof(lily_value *) * count);
    lily_type *integer_type = vm->integer_type;

    int i;
    for (i = 0;i < count;i++) {
        lily_value *v = lily_malloc(sizeof(lily_value));
        v->type = integer_type;
        v->flags = VAL_IS_NIL;
        upvalues[i] = v;
    }

    closure_func->closure_data = d;
    closure_func->refcount = 1;
    d->upvalues = upvalues;
    d->refcount = 1;
    d->num_upvalues = count;

    lily_raw_value v = {.function = closure_func};
    lily_move_raw_value(vm, result, 0, v);
    return upvalues;
}

/*  This opcode is written when either a function or lambda is created within
    a defined function. It creates a copy of a given function, but stores
    closure information within the copy. The copy created is used in place of
    the given function, as it has upvalue information that the original does
    not.
    By doing this, calls to the function copy will load the function with
    closures onto the stack. */
static void do_o_create_function(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_tie *function_literal = vm->readonly_table[code[1]];
    lily_function_val *raw_func = function_literal->value.function;
    lily_function_val *closure_copy = lily_new_function_copy(raw_func);
    lily_value *closure_reg = vm_regs[code[2]];

    add_gc_item(vm, function_literal->type,
            (lily_generic_gc_val *)closure_copy);

    lily_function_val *active_closure = closure_reg->value.function;
    closure_copy->closure_data = active_closure->closure_data;
    closure_copy->closure_data->refcount++;

    lily_raw_value v = {.function = closure_copy};

    lily_value **upvalues = closure_copy->closure_data->upvalues;
    upvalues[code[3]]->type = closure_reg->type;
    lily_move_raw_value(vm, upvalues[code[3]], 0, v);
}

/*  This is written at the top of a function that uses closures but is not a
    class method. Because of how o_create_function works, the most recent call
    (this function) has closure data in the function part of the stack. This is
    as simple as drawing that data out. */
static lily_value **do_o_load_closure(lily_vm_state *vm, uint16_t *code)
{
    lily_function_val *f = vm->call_chain->function;
    lily_raw_value v = {.function = f};
    lily_value *result = vm->vm_regs[code[2]];

    /* This isn't using assign because there is no proper lily value that is
       holding closure. Instead, do a move and manually bump the ref. */
    lily_move_raw_value(vm, result, 0, v);
    f->refcount++;

    return f->closure_data->upvalues;
}

/*  This is written at the top of a class method when the class has methods that
    use upvalues. Unlike with typical functions, class methods can be referenced
    statically outside of the class. Because class methods always take self as
    their first parameter, the class will hold a closure that this will pull out
    and put into a local register. */
static lily_value **do_o_load_class_closure(lily_vm_state *vm, uint16_t *code,
        int code_pos)
{
    do_o_get_property(vm, code, code_pos);
    lily_value *closure_reg = vm->vm_regs[code[code_pos + 4]];
    lily_function_val *f = closure_reg->value.function;

    /* Don't adjust any refcounts here, because do_o_get_property will use
       lily_assign_value which will do that for us. */

    return f->closure_data->upvalues;
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
    lily_instance_val *ival = lily_malloc(sizeof(lily_instance_val));

    ival->values = lily_malloc(2 * sizeof(lily_value *));
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

    lily_raw_value v = {.instance = ival};
    lily_move_raw_value(vm, result, 0, v);
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
    const char *except_name;
    lily_class *raised_class;

    if (vm->catch_top == NULL)
        return 0;

    if (vm->raiser->exception_type == NULL) {
        except_name = lily_name_for_error(vm->raiser);
        /* This is called instead of lily_find_class so that the exception will
           be dynaloaded if it needs to be. Also, keep in mind that needing to
           dynaload an exception at this stage does not mean that the catch will
           fail (the user may have put up a 'except Exception' somewhere). */
        raised_class = lily_maybe_dynaload_class(vm->parser, NULL, except_name);
    }
    else {
        lily_type *raise_type = vm->raiser->exception_type;
        raised_class = raise_type->cls;
        except_name = raised_class->name;
    }

    lily_vm_catch_entry *catch_iter = vm->catch_top;
    lily_value *catch_reg = NULL;
    lily_value **stack_regs;
    int do_unbox, jump_location, match;

    match = 0;

    while (catch_iter != NULL) {
        lily_call_frame *call_frame = catch_iter->call_frame;
        uint16_t *code = call_frame->function->code;
        /* A try block is done when the next jump is at 0 (because 0 would
           always be going back, which is illogical otherwise). */
        jump_location = code[catch_iter->code_pos];
        stack_regs = vm->regs_from_main + catch_iter->offset_from_main;

        while (jump_location != 0) {
            /* Instead of the vm hopping around to different o_except blocks,
               this function visits them to find out which (if any) handles
               the current exception.
               +1 is the line number (for debug), +2 is the spot for the next
               jump, and +3 is a register that holds a value to store this
               particular exception. */
            int next_location = code[jump_location + 2];
            catch_reg = stack_regs[code[jump_location + 4]];
            lily_class *catch_class = catch_reg->type->cls;
            if (catch_class == raised_class ||
                lily_check_right_inherits_or_is(catch_class, raised_class)) {
                /* ...So that execution resumes from within the except block. */
                do_unbox = code[jump_location + 3];
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
        if (do_unbox)
            make_proper_exception_val(vm, raised_class, catch_reg);

        vm->call_chain = catch_iter->call_frame;
        vm->call_depth = catch_iter->call_frame_depth;
        vm->vm_regs = stack_regs;
        vm->call_chain->code_pos = jump_location;
        /* Each try block can only successfully handle one exception, so use
           ->prev to prevent using the same block again. */
        vm->catch_top = catch_iter->prev;
        if (vm->catch_top != NULL)
            vm->catch_chain = vm->catch_top;
        else
            vm->catch_chain = catch_iter;
    }

    return match;
}

/******************************************************************************/
/* Foreign call API                                                           */
/******************************************************************************/

/* This handles calling a foreign function which will call a native one.
   When a foreign function is entered, the vm does not adjust vm_regs. This is
   intentional, because builtin functions wouldn't be able to get their values
   if the registers were adjusted. */

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
    int regs_adjust = vm->call_chain->prev->regs_used +
            vm->call_chain->regs_used;

    /* Make it so the callee's register indexes target the right things. */
    vm->vm_regs += regs_adjust;

    vm->call_chain = vm->call_chain->next;
    /* The foreign entry added itself to the stack properly, so just add one
       for the native entry. */
    vm->call_depth++;


    lily_vm_execute(vm);

    /* The return done adjusts for the foreign callee, but not for the native
       caller. Do this or the foreign caller will have busted registers. */
    vm->vm_regs -= vm->call_chain->prev->regs_used;
}

/*  lily_vm_get_foreign_reg
    Obtain a value, adjusted for the function to be called. 0 is the value of the
    return (if there is one). Otherwise, this may not be useful. */
lily_value *lily_vm_get_foreign_reg(lily_vm_state *vm, int reg_pos)
{
    lily_value **vm_regs = vm->vm_regs;
    int load_start = vm->call_chain->prev->regs_used +
            vm->call_chain->regs_used;

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
    int load_start = vm->call_chain->prev->regs_used +
                     vm->call_chain->regs_used;

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

        lily_deref(reg);

        reg->type = info[i].type;
        reg->flags = VAL_IS_NIL;
    }
}

/*  lily_vm_foreign_prep
    Prepare the given vm for a call from a foreign function. This will ensure
    that the vm has stack space, and also initialize the registers to the
    proper types. The stack entry is updated, but the stack depth is not
    updated (so that raises show the proper value).

    vm:     The vm to prepare to enter.
    tocall: A value holding a function to call. */
void lily_vm_foreign_prep(lily_vm_state *vm, lily_value *to_call)
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

    /* Step 3: If there's a return register, set it to the proper type. */
    int function_vals_used = vm->call_chain->prev->regs_used;

    if (function_val_return_type != NULL) {
        lily_value *foreign_reg = vm->vm_regs[function_vals_used];
        /* Set it to the right type, in case something looks at it later. */
        foreign_reg->type = function_val_return_type;
    }

    seed_registers(vm, function_val, callee_start);

    lily_call_frame *foreign_frame = vm->call_chain;

    /* Step 4: Make sure there is a spot for the native entry that will be
       added. The order and later usage of foreign_frame is vital, because
       this function will update vm->call_chain. */
    if (vm->call_chain->next == NULL)
        add_call_frame(vm);

    /* Step 5: Update the foreign function that was entered so that its code
       returns to 'o_return_from_vm' to get back into that function. */
    foreign_frame->code = vm->foreign_code;
    foreign_frame->code_pos = 0;
    foreign_frame->regs_used = (function_val_return_type != NULL);
    foreign_frame->return_reg = -1;
    foreign_frame->upvalues = NULL;
    foreign_frame->build_value = NULL;

    /* Step 6: Set the second stack entry (the native function). */
    lily_call_frame *native_frame = foreign_frame->next;
    native_frame->code = function_val->code;
    native_frame->code_pos = 0;
    native_frame->regs_used = function_val->reg_count;
    native_frame->return_reg = 0;
    native_frame->function = function_val;
    native_frame->line_num = 0;
    native_frame->upvalues = NULL;
    native_frame->build_value = NULL;

    /* lily_vm_foreign_call will bump this up before each call. The goal
       is for control to seamlessly return to lily_vm_execute which will
       drop the stack back to the right place. */
    vm->call_chain = foreign_frame;
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
            if ((right->flags & VAL_IS_NOT_DEREFABLE) == 0)
                right->value.generic->refcount++;

            lily_deref(left);
        }

        left->value = right->value;
        left->flags = right->flags;
    }
}

/*  lily_move_raw_value
    This is nearly as handy as lily_assign_value. The purpose of this function
    is to put a raw value (likely one that was just made) into another value.

    left:      This is a proper value which will receive the new value. If it
               is marked as refcounted (but not nil or protected), it will
               receive a deref.
    flags:     The new flags to set onto left.
    raw_right: A raw value that is of the same type as left. This value will not
               receive a ref bump.

    Do not pass values of type 'any' to this function, or enum/variant values.
    It cannot handle those (I'll add that functionality when it's needed so I
    have something to test it with!).

    This function is useful in cases where a newly-created raw value needs to be
    put into a register. Bumping the new value would cause it to have two refs,
    which is wrong. */
void lily_move_raw_value(lily_vm_state *vm, lily_value *left,
        int flags, lily_raw_value raw_right)
{
    lily_class *cls = left->type->cls;
    lily_deref(left);

    left->value = raw_right;
    left->flags = flags | (cls->flags & VAL_IS_PRIMITIVE);
}

/*  This is used by modules to raise exceptions which are marked as dynaloaded
    by that same module. Because this should only be called from a module
    function, it is assumed that the last call is a function of that module.

    This function sets the raiser's error, but does not actually trigger it to
    raise. The caller should use lily_vm_raise_prepared to do that. This is
    intentional, as the module may need to do some tidying up before the raise
    is to be done. */
void lily_vm_set_error(lily_vm_state *vm, lily_base_seed *error_seed,
        const char *msg)
{
    lily_import_entry *seed_import = vm->call_chain->function->import;
    lily_class *raise_class = lily_maybe_dynaload_class(vm->parser, seed_import,
            error_seed->name);

    /* An exception that has been dynaloaded will never have generics, and thus
       will always have a default type. It is thus safe to grab the type of the
       newly-made class. */
    lily_raiser_set_error(vm->raiser, raise_class->type, msg);
}

/*  This acts like lily_vm_set_error, except that it immediately triggers the
    error instead of waiting for a corresponding call to lily_vm_raise_prepared.
    This should be used in situations where there is no need to do cleanup of
    any sort.

    This is meant to be called by modules, using a seed that may need to be
    dynaloaded. Internal functions won't need to call this. */
void lily_vm_module_raise(lily_vm_state *vm, lily_base_seed *error_seed,
        const char *msg)
{
    lily_import_entry *seed_import = vm->call_chain->function->import;
    lily_class *raise_class = lily_maybe_dynaload_class(vm->parser, seed_import,
            error_seed->name);

    lily_raise_type_and_msg(vm->raiser, raise_class->type, msg);
}

/*  Cause the raiser to trigger an error registered by lily_vm_set_error. This
    is meant solely for modules: Internal functions will never use this. A
    module should use this instead of using vm->raiser (since the memory layout
    of the vm may change during a release). */
void lily_vm_raise_prepared(lily_vm_state *vm)
{
    lily_raise_prepared(vm->raiser);
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
    * Load any tied values into global registers.
    * Set stack entry 0 (__main__'s entry). */
void lily_vm_prep(lily_vm_state *vm, lily_symtab *symtab)
{
    lily_function_val *main_function = symtab->main_function;
    int i;

    /* This has to be set before grow_vm_registers because that initializes the
       registers to integer's type. */
    vm->integer_type = symtab->integer_class->type;

    if (main_function->reg_count > vm->max_registers) {
        grow_vm_registers(vm, main_function->reg_count);
        /* grow_vm_registers will move vm->vm_regs (which is supposed to be
           local). That works everywhere...but here. Fix vm_regs back to the
           start because we're still in __main__. */
        vm->vm_regs = vm->regs_from_main;
    }
    lily_value **regs_from_main = vm->regs_from_main;

    for (i = vm->prep_id_start;i < main_function->reg_count;i++) {
        lily_value *reg = regs_from_main[i];
        lily_register_info seed = main_function->reg_info[i];

        /* This allows opcodes to copy over a register value without checking
           if VAL_IS_NIL is set. This is fine, because the value won't actually
           be used if VAL_IS_NIL is set (optimization!) */
        reg->flags = VAL_IS_NIL;
        reg->type = seed.type;
    }

    /* Zap only the slots that new globals need next time. */
    vm->prep_id_start = i;

    /* Symtab is guaranteed to always have a non-NULL tie because the sys
       package creates a tie. */
    if (vm->symtab->foreign_ties)
        load_foreign_ties(vm);

    if (vm->readonly_count != symtab->next_readonly_spot)
        setup_readonly_table(vm);

    vm->num_registers = main_function->reg_count;

    lily_call_frame *first_frame = vm->call_chain;
    first_frame->function = main_function;
    first_frame->code = main_function->code;
    first_frame->regs_used = main_function->reg_count;
    first_frame->return_reg = 0;
    first_frame->code_pos = 0;
    first_frame->build_value = NULL;
    vm->call_depth = 1;
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
    lily_value **regs_from_main;
    lily_value **vm_regs;
    int i, num_registers, max_registers;
    lily_type *cast_type;
    register int64_t for_temp;
    /* This unfortunately has to be volatile because otherwise calltrace() and
       traceback tend to be 'off'. */
    register volatile int code_pos;
    register lily_value *lhs_reg, *rhs_reg, *loop_reg, *step_reg;
    register lily_tie *readonly_val;
    /* These next two are used so that INTDBL operations have a fast way to
       check the type of a register. */
    lily_type *integer_type = vm->symtab->integer_class->type;
    lily_type *double_type = vm->symtab->double_class->type;
    lily_function_val *fval;
    lily_value **upvalues = NULL;

    lily_call_frame *current_frame = vm->call_chain;

    f = current_frame->function;
    code = f->code;

    /* Initialize local vars from the vm state's vars. */
    vm_regs = vm->vm_regs;
    regs_from_main = vm->regs_from_main;
    num_registers = vm->num_registers;
    max_registers = vm->max_registers;
    code_pos = 0;

    /* Only install a raiser jump on the first vm entry. */
    if (current_frame->prev == NULL) {
        if (setjmp(vm->raiser->jumps[vm->raiser->jump_pos]) == 0)
            vm->raiser->jump_pos++;
        else {
            /* If the current function is a native one, then fix the line
               number of it. Otherwise, leave the line number alone. */
            if (current_frame->function->code != NULL)
                current_frame->line_num = current_frame->code[code_pos+1];

            if (maybe_catch_exception(vm) == 0)
                /* Couldn't catch it. Jump back into parser, which will jump
                   back to the caller to give them the bad news. */
                longjmp(vm->raiser->jumps[vm->raiser->jump_pos-2], 1);
            else {
                /* The exception was caught, so resync local data. */
                current_frame = vm->call_chain;
                code = current_frame->code;
                code_pos = current_frame->code_pos;
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
            case o_get_readonly:
                readonly_val = vm->readonly_table[code[code_pos+2]];
                lhs_reg = vm_regs[code[code_pos+3]];

                lily_deref(lhs_reg);

                lhs_reg->value = readonly_val->value;
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
                if (vm->call_depth > 100)
                    lily_raise(vm->raiser, lily_RecursionError,
                            "Function call recursion limit reached.\n");

                if (current_frame->next == NULL)
                    add_call_frame(vm);

                if (code[code_pos+2] == 1)
                    fval = vm->readonly_table[code[code_pos+3]]->value.function;
                else
                    fval = vm_regs[code[code_pos+3]]->value.function;

                int j = code[code_pos+4];
                current_frame->line_num = code[code_pos+1];
                current_frame->code_pos = code_pos + j + 6;
                current_frame->upvalues = upvalues;

                if (fval->code != NULL) {
                    int register_need = fval->reg_count + num_registers;

                    if (register_need > max_registers) {
                        grow_vm_registers(vm, register_need);
                        /* Don't forget to update local info... */
                        regs_from_main = vm->regs_from_main;
                        vm_regs        = vm->vm_regs;
                        max_registers  = vm->max_registers;
                    }

                    /* Prepare the registers for what the function wants.
                       Afterward, update num_registers since prep_registers
                       changes it. */
                    prep_registers(vm, fval, code+code_pos);
                    num_registers = vm->num_registers;

                    vm_regs = vm_regs + current_frame->regs_used;
                    vm->vm_regs = vm_regs;

                    current_frame->return_reg =
                        -(current_frame->function->reg_count - code[code_pos+5+j]);

                    /* !PAST HERE TARGETS THE NEW FRAME! */

                    current_frame = current_frame->next;
                    vm->call_chain = current_frame;

                    current_frame->function = fval;
                    current_frame->regs_used = fval->reg_count;
                    current_frame->code = fval->code;
                    current_frame->upvalues = NULL;
                    vm->call_depth++;
                    code = fval->code;
                    code_pos = 0;
                    upvalues = NULL;
                }
                else {
                    lily_foreign_func func = fval->foreign_func;

                    current_frame = current_frame->next;
                    vm->call_chain = current_frame;

                    current_frame->function = fval;
                    current_frame->line_num = -1;
                    current_frame->code = NULL;
                    current_frame->build_value = NULL;
                    current_frame->upvalues = NULL;
                    /* An offset from main does not have to be included, because
                       foreign functions don't have code which can catch an
                       exception. */
                    vm->call_depth++;

                    func(vm, j, code+code_pos+5);
                    /* This function may have called the vm, thus growing the
                       number of registers. Copy over important data if that's
                       happened. */
                    if (vm->max_registers != max_registers) {
                        regs_from_main = vm->regs_from_main;
                        vm_regs        = vm->vm_regs;
                        max_registers  = vm->max_registers;
                    }

                    current_frame = current_frame->prev;
                    vm->call_chain = current_frame;

                    code_pos += 6 + j;
                    vm->call_depth--;
                }
            }
                break;
            case o_unary_not:
                lhs_reg = vm_regs[code[code_pos+2]];

                rhs_reg = vm_regs[code[code_pos+3]];
                rhs_reg->flags = VAL_IS_PRIMITIVE;
                rhs_reg->value.integer = !(lhs_reg->value.integer);
                code_pos += 4;
                break;
            case o_unary_minus:
                lhs_reg = vm_regs[code[code_pos+2]];

                rhs_reg = vm_regs[code[code_pos+3]];
                rhs_reg->flags = VAL_IS_PRIMITIVE;
                rhs_reg->value.integer = -(lhs_reg->value.integer);
                code_pos += 4;
                break;
            case o_return_val:
                /* Note: Upon function entry, vm_regs is moved so that 0 is the
                   start of the new function's registers.
                   Because of this, the register return is a -negative- index
                   that goes back into the caller's stack. */

                lhs_reg = vm_regs[current_frame->prev->return_reg];
                rhs_reg = vm_regs[code[code_pos+2]];
                lily_assign_value(vm, lhs_reg, rhs_reg);

                /* DO NOT BREAK HERE.
                   These two do the same thing from here on, so fall through to
                   share code. */
            case o_return_noval:
                current_frame->build_value = NULL;

                current_frame = current_frame->prev;
                vm->call_chain = current_frame;
                vm->call_depth--;

                /* The registers that the function last entered are not in use
                   anymore, so they don't count now. */
                num_registers -= current_frame->next->regs_used;
                vm->num_registers = num_registers;
                /* vm_regs adjusts by the count of the now-current function so
                   that vm_regs[0] is the 0 of the caller. */
                vm_regs = vm_regs - current_frame->regs_used;
                vm->vm_regs = vm_regs;
                upvalues = current_frame->upvalues;
                code = current_frame->code;
                code_pos = current_frame->code_pos;
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
            case o_get_property:
                do_o_get_property(vm, code, code_pos);
                code_pos += 5;
                break;
            case o_set_item:
                do_o_set_item(vm, code, code_pos);
                code_pos += 5;
                break;
            case o_set_property:
                do_o_set_property(vm, code, code_pos);
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
                    current_frame->line_num = current_frame->code[code_pos+1];

                    lily_raise(vm->raiser, lily_BadTypecastError,
                            "Cannot cast any containing type '^T' to type '^T'.\n",
                            rhs_reg->type, lhs_reg->type);
                }

                code_pos += 4;
                break;
            case o_create_function:
                do_o_create_function(vm, code + code_pos);
                code_pos += 4;
                break;
            case o_set_upvalue:
                lhs_reg = upvalues[code[code_pos + 2]];
                rhs_reg = vm_regs[code[code_pos + 3]];
                if (lhs_reg->flags & VAL_IS_NIL)
                    lhs_reg->type = rhs_reg->type;

                lily_assign_value(vm, lhs_reg, rhs_reg);
                code_pos += 4;
                break;
            case o_get_upvalue:
                lhs_reg = vm_regs[code[code_pos + 3]];
                rhs_reg = upvalues[code[code_pos + 2]];
                lily_assign_value(vm, lhs_reg, rhs_reg);
                code_pos += 4;
                break;
            case o_setup_optargs:
                do_o_setup_optargs(vm, code, code_pos);
                code_pos += 2 + code[code_pos + 1];
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
            case o_push_try:
            {
                if (vm->catch_chain->next == NULL)
                    add_catch_entry(vm);

                lily_vm_catch_entry *catch_entry = vm->catch_chain;
                catch_entry->call_frame = current_frame;
                catch_entry->call_frame_depth = vm->call_depth;
                catch_entry->code_pos = code_pos + 2;
                catch_entry->offset_from_main = (int64_t)(regs_from_main - vm_regs);

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
            case o_create_closure:
                upvalues = do_o_create_closure(vm, code+code_pos);
                code_pos += 4;
                break;
            case o_load_class_closure:
                upvalues = do_o_load_class_closure(vm, code, code_pos);
                code_pos += 5;
                break;
            case o_load_closure:
                upvalues = do_o_load_closure(vm, code+code_pos);
                code_pos += 3;
                break;
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

                    step_reg->flags = VAL_IS_PRIMITIVE;
                }
                else if (step_reg->value.integer == 0) {
                    lily_raise(vm->raiser, lily_ValueError,
                               "for loop step cannot be 0.\n");
                }

                loop_reg->value.integer = lhs_reg->value.integer;
                loop_reg->flags = VAL_IS_PRIMITIVE;

                code_pos += 7;
                break;
            case o_return_from_vm:
                /* Only the first entry to the vm adds a stack entry... */
                if (current_frame->prev == NULL)
                    vm->raiser->jump_pos--;
                return;
        }
    }
}
