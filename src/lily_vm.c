#include <stddef.h>
#include <string.h>

#include "lily_opcode.h"
#include "lily_vm.h"
#include "lily_parser.h"
#include "lily_value_stack.h"
#include "lily_value_flags.h"
#include "lily_move.h"

#include "lily_api_hash.h"
#include "lily_api_alloc.h"
#include "lily_api_options.h"
#include "lily_api_value.h"

extern void lily_string_subscript(lily_vm_state *, lily_value *, lily_value *,
        lily_value *);
extern uint64_t siphash24(const void *src, unsigned long src_sz, const char key[16]);
extern lily_gc_entry *lily_gc_stopper;
/* This isn't included in a header file because only vm should use this. */
void lily_destroy_value(lily_value *);

#define INTEGER_OP(OP) \
lhs_reg = vm_regs[code[2]]; \
rhs_reg = vm_regs[code[3]]; \
vm_regs[code[4]]->value.integer = \
lhs_reg->value.integer OP rhs_reg->value.integer; \
vm_regs[code[4]]->flags = VAL_IS_INTEGER; \
code += 5;

#define INTDBL_OP(OP) \
lhs_reg = vm_regs[code[2]]; \
rhs_reg = vm_regs[code[3]]; \
if (lhs_reg->flags & VAL_IS_DOUBLE) { \
    if (rhs_reg->flags & VAL_IS_DOUBLE) \
        vm_regs[code[4]]->value.doubleval = \
        lhs_reg->value.doubleval OP rhs_reg->value.doubleval; \
    else \
        vm_regs[code[4]]->value.doubleval = \
        lhs_reg->value.doubleval OP rhs_reg->value.integer; \
} \
else \
    vm_regs[code[4]]->value.doubleval = \
    lhs_reg->value.integer OP rhs_reg->value.doubleval; \
vm_regs[code[4]]->flags = VAL_IS_DOUBLE; \
code += 5;

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
lhs_reg = vm_regs[code[2]]; \
rhs_reg = vm_regs[code[3]]; \
if (lhs_reg->flags & VAL_IS_DOUBLE) { \
    if (rhs_reg->flags & VAL_IS_DOUBLE) \
        vm_regs[code[4]]->value.integer = \
        (lhs_reg->value.doubleval OP rhs_reg->value.doubleval); \
    else \
        vm_regs[code[4]]->value.integer = \
        (lhs_reg->value.doubleval OP rhs_reg->value.integer); \
} \
else if (lhs_reg->flags & VAL_IS_INTEGER) { \
    if (rhs_reg->flags & VAL_IS_INTEGER) \
        vm_regs[code[4]]->value.integer =  \
        (lhs_reg->value.integer OP rhs_reg->value.integer); \
    else \
        vm_regs[code[4]]->value.integer = \
        (lhs_reg->value.integer OP rhs_reg->value.doubleval); \
} \
else if (lhs_reg->flags & VAL_IS_STRING) { \
    vm_regs[code[4]]->value.integer = \
    strcmp(lhs_reg->value.string->string, \
           rhs_reg->value.string->string) STRINGOP; \
} \
else { \
    vm->pending_line = code[1]; \
    vm_regs[code[4]]->value.integer = \
    lily_eq_value(vm, lhs_reg, rhs_reg) OP 1; \
} \
vm_regs[code[4]]->flags = VAL_IS_BOOLEAN; \
code += 5;

#define COMPARE_OP(OP, STRINGOP) \
lhs_reg = vm_regs[code[2]]; \
rhs_reg = vm_regs[code[3]]; \
if (lhs_reg->flags & VAL_IS_DOUBLE) { \
    if (rhs_reg->flags & VAL_IS_DOUBLE) \
        vm_regs[code[4]]->value.integer = \
        (lhs_reg->value.doubleval OP rhs_reg->value.doubleval); \
    else \
        vm_regs[code[4]]->value.integer = \
        (lhs_reg->value.doubleval OP rhs_reg->value.integer); \
} \
else if (lhs_reg->flags & VAL_IS_INTEGER) { \
    if (rhs_reg->flags & VAL_IS_INTEGER) \
        vm_regs[code[4]]->value.integer =  \
        (lhs_reg->value.integer OP rhs_reg->value.integer); \
    else \
        vm_regs[code[4]]->value.integer = \
        (lhs_reg->value.integer OP rhs_reg->value.doubleval); \
} \
else if (lhs_reg->flags & VAL_IS_STRING) { \
    vm_regs[code[4]]->value.integer = \
    strcmp(lhs_reg->value.string->string, \
           rhs_reg->value.string->string) STRINGOP; \
} \
vm_regs[code[4]]->flags = VAL_IS_BOOLEAN; \
code += 5;

/** If you're interested in working on the vm, or having trouble with it, here's
    some advice that might make things easier.

    * The vm uses a mix of refcounting and a gc. Anything that has subvalues
      will get a gc tag. Values that can be destroyed are destroy-able through
      pure ref/deref (and that will zap the tag to prevent a double free), or
      through the gc if it comes to that.

    * Forgetting to increase a ref typically shows itself through invalid reads
      and/or writes during garbage collection.

    * -g is used to set the number of gc tags allowed at once. If Lily crashes
      at a certain number, then a value is missing a gc tag. **/

/* Foreign functions set this as their code so that the vm will exit when they
   are to be returned from. */
static uint16_t foreign_code[1] = {o_return_from_vm};

/***
 *      ____       _
 *     / ___|  ___| |_ _   _ _ __
 *     \___ \ / _ \ __| | | | '_ \
 *      ___) |  __/ |_| |_| | |_) |
 *     |____/ \___|\__|\__,_| .__/
 *                          |_|
 */

static void add_call_frame(lily_vm_state *);
static void invoke_gc(lily_vm_state *);

lily_vm_state *lily_new_vm_state(lily_options *options,
        lily_raiser *raiser)
{
    lily_vm_state *vm = lily_malloc(sizeof(lily_vm_state));
    vm->data = options->data;
    vm->gc_threshold = options->gc_start;
    vm->gc_multiplier = options->gc_multiplier;
    if (vm->gc_multiplier > 16)
        vm->gc_multiplier = 16;

    vm->sipkey = options->sipkey;
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
    vm->catch_chain = NULL;
    vm->symtab = NULL;
    vm->readonly_table = NULL;
    vm->readonly_count = 0;
    vm->call_chain = NULL;
    vm->class_count = 0;
    vm->class_table = NULL;
    vm->stdout_reg = NULL;
    vm->exception_value = NULL;
    vm->pending_line = 0;

    add_call_frame(vm);

    lily_vm_catch_entry *catch_entry = lily_malloc(sizeof(lily_vm_catch_entry));
    catch_entry->prev = NULL;
    catch_entry->next = NULL;

    vm->catch_chain = catch_entry;

    return vm;
}

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

    /* If there are any entries left over, then do a final gc pass that will
       destroy the tagged values. */
    if (vm->gc_live_entry_count)
        invoke_gc(vm);

    destroy_gc_entries(vm);

    lily_free(vm->class_table);
    lily_free(vm);
}

/***
 *       ____    ____
 *      / ___|  / ___|
 *     | |  _  | |
 *     | |_| | | |___
 *      \____|  \____|
 *
 */

static void gc_mark(int, lily_value *);

/* This is Lily's garbage collector. It runs in multiple stages:
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
      * lily_destroy_value will collect everything inside a non-circular value,
        but not the value itself. It will set last_pass to -1 when it does that.
        This is necessary because it's possible that a value may be visited
        multiple times. If it's deleted during this step, then extra visits will
        trigger invalid reads.
   3: Stage 1 skipped registers that are not in-use, because Lily just hasn't
      gotten around to clearing them yet. However, some of those registers may
      contain a value that has a gc_entry that indicates that the value is to be
      destroyed. It's -very- important that these registers be marked as nil so
      that prep_registers will not try to deref a value that has been destroyed
      by the gc.
   4: Finally, destroy any values that stage 2 didn't clear.
      Absolutely nothing is using these now, so it's safe to destroy them. */
static void invoke_gc(lily_vm_state *vm)
{
    /* This is (sort of) a mark-and-sweep garbage collector. This is called when
       a certain number of allocations have been done. Take note that values
       can be destroyed by deref. However, those values will have the gc_entry's
       value set to NULL as an indicator. */
    vm->gc_pass++;

    lily_value **regs_from_main = vm->regs_from_main;
    int pass = vm->gc_pass;
    int i;
    lily_gc_entry *gc_iter;

    /* Stage 1: Go through all registers and use the appropriate gc_marker call
                that will mark every inner value that's visible. */
    for (i = 0;i < vm->num_registers;i++) {
        lily_value *reg = regs_from_main[i];
        if (reg->flags & VAL_IS_GC_SWEEPABLE)
            gc_mark(pass, reg);
    }

    /* Stage 2: Start destroying everything that wasn't marked as visible.
                Don't forget to check ->value for NULL in case the value was
                destroyed through normal ref/deref means. */
    for (gc_iter = vm->gc_live_entries;
         gc_iter;
         gc_iter = gc_iter->next) {
        if (gc_iter->last_pass != pass &&
            gc_iter->value.generic != NULL) {
            /* This tells value destroy to just hollow the value since it may be
               visited multiple times. */
            gc_iter->last_pass = -1;
            lily_destroy_value((lily_value *)gc_iter);
        }
    }

    /* Stage 3: Check registers not currently in use to see if they hold a
                value that's going to be collected. If so, then mark the
                register as nil so that the value will be cleared later. */
    for (i = vm->num_registers;i < vm->max_registers;i++) {
        lily_value *reg = regs_from_main[i];
        if (reg->flags & VAL_IS_GC_TAGGED &&
            reg->value.gc_generic->gc_entry == lily_gc_stopper) {
            reg->flags = 0;
        }
    }

    /* Stage 4: Delete the values that stage 2 didn't delete.
                Nothing is using them anymore. Also, sort entries into those
                that are living and those that are no longer used. */
    i = 0;
    lily_gc_entry *new_live_entries = NULL;
    lily_gc_entry *new_spare_entries = vm->gc_spare_entries;
    lily_gc_entry *iter_next = NULL;
    gc_iter = vm->gc_live_entries;

    while (gc_iter) {
        iter_next = gc_iter->next;

        if (gc_iter->last_pass == -1) {
            lily_free(gc_iter->value.generic);

            gc_iter->next = new_spare_entries;
            new_spare_entries = gc_iter;
        }
        else {
            i++;
            gc_iter->next = new_live_entries;
            new_live_entries = gc_iter;
        }

        gc_iter = iter_next;
    }

    /* Did the sweep reclaim enough objects? If not, then increase the threshold
       to prevent spamming sweeps when everything is alive. */
    if (vm->gc_threshold <= i)
        vm->gc_threshold *= vm->gc_multiplier;

    vm->gc_live_entry_count = i;
    vm->gc_live_entries = new_live_entries;
    vm->gc_spare_entries = new_spare_entries;
}

static void dynamic_marker(int pass, lily_value *v)
{
    if (v->flags & VAL_IS_GC_TAGGED) {
        lily_gc_entry *e = v->value.dynamic->gc_entry;
        if (e->last_pass == pass)
            return;

        e->last_pass = pass;
    }

    lily_value *inner_value = v->value.dynamic->inner_value;

    if (inner_value->flags & VAL_IS_GC_SWEEPABLE)
        gc_mark(pass, inner_value);
}

static void list_marker(int pass, lily_value *v)
{
    if (v->flags & VAL_IS_GC_TAGGED) {
        /* Only instances/enums that pass through here are tagged. */
        lily_gc_entry *e = v->value.instance->gc_entry;
        if (e->last_pass == pass)
            return;

        e->last_pass = pass;
    }

    lily_list_val *list_val = v->value.list;
    int i;

    for (i = 0;i < list_val->num_values;i++) {
        lily_value *elem = list_val->elems[i];

        if (elem->flags & VAL_IS_GC_SWEEPABLE)
            gc_mark(pass, elem);
    }
}

static void hash_marker(int pass, lily_value *v)
{
    lily_hash_val *hash_val = v->value.hash;
    lily_hash_elem *elem_iter = hash_val->elem_chain;
    while (elem_iter) {
        lily_value *elem_value = elem_iter->elem_value;
        gc_mark(pass, elem_value);

        elem_iter = elem_iter->next;
    }
}

static void function_marker(int pass, lily_value *v)
{
    if (v->flags & VAL_IS_GC_TAGGED) {
        lily_gc_entry *e = v->value.function->gc_entry;
        if (e->last_pass == pass)
            return;

        e->last_pass = pass;
    }

    lily_function_val *function_val = v->value.function;

    lily_value **upvalues = function_val->upvalues;
    int count = function_val->num_upvalues;
    int i;

    for (i = 0;i < count;i++) {
        lily_value *up = upvalues[i];
        if (up && (up->flags & VAL_IS_GC_SWEEPABLE))
            gc_mark(pass, up);
    }
}

static void gc_mark(int pass, lily_value *v)
{
    int flags = v->flags;
    if (flags & (VAL_IS_GC_TAGGED | VAL_IS_GC_SPECULATIVE)) {
        if (flags &
            (VAL_IS_LIST | VAL_IS_INSTANCE | VAL_IS_ENUM | VAL_IS_TUPLE))
            list_marker(pass, v);
        else if (flags & VAL_IS_HASH)
            hash_marker(pass, v);
        else if (flags & VAL_IS_DYNAMIC)
            dynamic_marker(pass, v);
        else if (flags & VAL_IS_FUNCTION)
            function_marker(pass, v);
    }
}

/* This will attempt to grab a spare entry and associate it with the value
   given. If there are no spare entries, then a new entry is made. These entries
   are how the gc is able to locate values later.

   If the number of living gc objects is at or past the threshold, then the
   collector will run BEFORE the association. This is intentional, as 'value' is
   not guaranteed to be in a register. */
void lily_tag_value(lily_vm_state *vm, lily_value *v)
{
    if (vm->gc_live_entry_count >= vm->gc_threshold)
        invoke_gc(vm);

    lily_gc_entry *new_entry;
    if (vm->gc_spare_entries != NULL) {
        new_entry = vm->gc_spare_entries;
        vm->gc_spare_entries = vm->gc_spare_entries->next;
    }
    else
        new_entry = lily_malloc(sizeof(lily_gc_entry));

    new_entry->value.gc_generic = v->value.gc_generic;
    new_entry->last_pass = 0;
    new_entry->flags = v->flags;

    new_entry->next = vm->gc_live_entries;
    vm->gc_live_entries = new_entry;

    /* Attach the gc_entry to the value so the caller doesn't have to. */
    v->value.gc_generic->gc_entry = new_entry;
    vm->gc_live_entry_count++;

    v->flags |= VAL_IS_GC_TAGGED;
}

/***
 *      ____            _     _
 *     |  _ \ ___  __ _(_)___| |_ ___ _ __ ___
 *     | |_) / _ \/ _` | / __| __/ _ \ '__/ __|
 *     |  _ <  __/ (_| | \__ \ ||  __/ |  \__ \
 *     |_| \_\___|\__, |_|___/\__\___|_|  |___/
 *                |___/
 */

/** Lily is a register-based vm. This means that each call has a block of values
    that belong to it. Upon a call's entry, the types of the registers are set,
    and values are put into the registers. Each register has a type that it will
    retain through the lifetime of the call.

    This section deals with operations concerning registers. One area that is
    moderately difficult is handling generics. Lily does not create specialized
    concretely-typed functions in place of generic ones, but instead checks for
    soundness and defers things to vm-time. The vm is then tasked with changing
    a register with a seed type of, say, A, into whatever it should be for the
    given invocation. **/

/* This function ensures that 'register_need' more registers will be available.
   This may resize (and thus invalidate) vm->regs_from_main and vm->vm_regs. */
static void grow_vm_registers(lily_vm_state *vm, int register_need)
{
    lily_value **new_regs;
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
            sizeof(lily_value *));

    /* Realloc can move the pointer, so always recalculate vm_regs again using
       regs_from_main and the offset. */
    vm->regs_from_main = new_regs;
    vm->vm_regs = new_regs + reg_offset;

    /* Now create the registers as a bunch of empty values, to be filled in
       whenever they are needed. */
    for (;i < size;i++) {
        lily_value *v = lily_malloc(sizeof(lily_value));
        v->flags = 0;

        new_regs[i] = v;
    }

    vm->max_registers = size;
}

/* This is called to clear the values that reside in the non-parameter registers
   of a call. This is necessary because arthmetic operations assume that the
   target is not a refcounted register. */
static inline void scrub_registers(lily_vm_state *vm,
        lily_function_val *fval, int args_collected)
{
    lily_value **target_regs = vm->regs_from_main + vm->num_registers;
    for (;args_collected < fval->reg_count;args_collected++) {
        lily_value *reg = target_regs[args_collected];
        lily_deref(reg);

        reg->flags = 0;
    }
}

/* This is called to initialize the registers that 'fval' will need to types
   that it expects. Old values are given a deref. Parameters are copied over and
   given refs. */
static void prep_registers(lily_vm_state *vm, lily_function_val *fval,
        uint16_t *code)
{
    int register_need = vm->num_registers + fval->reg_count;
    int i;
    lily_value **input_regs = vm->vm_regs;
    lily_value **target_regs = vm->regs_from_main + vm->num_registers;

    /* A function's args always come first, so copy arguments over while clearing
       old values. */
    for (i = 0;i < code[3];i++) {
        lily_value *get_reg = input_regs[code[5+i]];
        lily_value *set_reg = target_regs[i];

        if (get_reg->flags & VAL_IS_DEREFABLE)
            get_reg->value.generic->refcount++;

        if (set_reg->flags & VAL_IS_DEREFABLE)
            lily_deref(set_reg);

        *set_reg = *get_reg;
    }

    if (i != fval->reg_count)
        scrub_registers(vm, fval, i);

    vm->num_registers = register_need;
}

void lily_push_integer(lily_vm_state *vm, int64_t i)
{
    if (vm->num_registers == vm->max_registers)
        grow_vm_registers(vm, vm->num_registers + 1);

    lily_move_integer(vm->regs_from_main[vm->num_registers], i);
    vm->num_registers++;
}

void lily_push_list(lily_vm_state *vm, lily_list_val *l)
{
    if (vm->num_registers == vm->max_registers)
        grow_vm_registers(vm, vm->num_registers + 1);

    lily_move_list_f(MOVE_DEREF_SPECULATIVE,
            vm->regs_from_main[vm->num_registers], l);
    vm->num_registers++;
}

void lily_push_value(lily_vm_state *vm, lily_value *v)
{
    if (vm->num_registers == vm->max_registers)
        grow_vm_registers(vm, vm->num_registers + 1);

    lily_assign_value(vm->regs_from_main[vm->num_registers], v);
    vm->num_registers++;
}


/***
 *      _   _      _
 *     | | | | ___| |_ __   ___ _ __ ___
 *     | |_| |/ _ \ | '_ \ / _ \ '__/ __|
 *     |  _  |  __/ | |_) |  __/ |  \__ \
 *     |_| |_|\___|_| .__/ \___|_|  |___/
 *                  |_|
 */

static void add_call_frame(lily_vm_state *vm)
{
    lily_call_frame *new_frame = lily_malloc(sizeof(lily_call_frame));

    /* This intentionally doesn't set anything but prev and next because the
       caller will have proper values for those. */
    new_frame->prev = vm->call_chain;
    new_frame->next = NULL;
    new_frame->return_target = NULL;
    new_frame->build_value = NULL;

    if (vm->call_chain != NULL)
        vm->call_chain->next = new_frame;

    vm->call_chain = new_frame;
}

static void add_catch_entry(lily_vm_state *vm)
{
    lily_vm_catch_entry *new_entry = lily_malloc(sizeof(lily_vm_catch_entry));

    vm->catch_chain->next = new_entry;
    new_entry->next = NULL;
    new_entry->prev = vm->catch_chain;
}

/***
 *      _____
 *     | ____|_ __ _ __ ___  _ __ ___
 *     |  _| | '__| '__/ _ \| '__/ __|
 *     | |___| |  | | | (_) | |  \__ \
 *     |_____|_|  |_|  \___/|_|  |___/
 *
 */

static const char *names[] = {
    "Exception",
    "IOError",
    "KeyError",
    "RuntimeError",
    "ValueError",
    "IndexError",
    "DivisionByZeroError"
};

/* This raises an error in the vm that won't have a proper value backing it. The
   id should be the id of some exception class. This may run a faux dynaload of
   the error, so that printing has a class name to go by. */
void lily_error(lily_vm_state *vm, uint8_t id, const char *message)
{
    lily_class *c = vm->class_table[id];
    if (c == NULL) {
        /* What this does is to kick parser's exception bootstrapping machinery
           into gear in order to load the exception that's needed. This is
           unfortunate, but the vm doesn't have a sane and easy way to properly
           build classes here. */
        c = lily_dynaload_exception(vm->parser,
                names[id - SYM_CLASS_EXCEPTION]);
        vm->class_table[id] = c;
    }

    lily_raise_class(vm->raiser, c, message);
}

/* Similar to lily_error, except this accept a format string and extra
   arguments. This is kept apart from lily_error because many callers do not
   have format strings. */
void lily_error_fmt(lily_vm_state *vm, uint8_t id, const char *fmt, ...)
{
    lily_msgbuf *msgbuf = vm->raiser->aux_msgbuf;

    lily_mb_flush(msgbuf);
    va_list var_args;
    va_start(var_args, fmt);
    lily_mb_add_fmt_va(msgbuf, fmt, var_args);
    va_end(var_args);

    lily_error(vm, id, lily_mb_get(msgbuf));
}

/* Raise KeyError with 'key' as the value of the message. */
static void key_error(lily_vm_state *vm, lily_value *key, uint16_t line_num)
{
    vm->pending_line = line_num;

    lily_msgbuf *msgbuf = vm->raiser->aux_msgbuf;

    if (key->flags & VAL_IS_STRING) {
        /* String values are required to be \0 terminated, so this is ok. */
        lily_mb_add_fmt(msgbuf, "\"^E\"", key->value.string->string);
    }
    else
        lily_mb_add_fmt(msgbuf, "%d", key->value.integer);

    lily_error(vm, SYM_CLASS_KEYERROR, lily_mb_get(msgbuf));
}

/* Raise IndexError, noting that 'bad_index' is, well, bad. */
static void boundary_error(lily_vm_state *vm, int bad_index)
{
    lily_msgbuf *msgbuf = vm->raiser->aux_msgbuf;
    lily_mb_flush(msgbuf);
    lily_mb_add_fmt(msgbuf, "Subscript index %d is out of range.",
            bad_index);

    lily_error(vm, SYM_CLASS_INDEXERROR, lily_mb_get(msgbuf));
}

/***
 *      ____        _ _ _   _
 *     | __ ) _   _(_) | |_(_)_ __  ___
 *     |  _ \| | | | | | __| | '_ \/ __|
 *     | |_) | |_| | | | |_| | | | \__ \
 *     |____/ \__,_|_|_|\__|_|_| |_|___/
 *
 */

static lily_list_val *build_traceback_raw(lily_vm_state *);

void lily_builtin_calltrace(lily_vm_state *vm)
{
    /* Nobody is going to care that the most recent function is calltrace, so
       omit that. */
    vm->call_chain = vm->call_chain->prev;
    vm->call_depth--;

    lily_list_val *traceback_val = build_traceback_raw(vm);

    vm->call_chain = vm->call_chain->next;
    vm->call_depth++;

    lily_return_list(vm, traceback_val);
}

static void do_print(lily_vm_state *vm, FILE *target, lily_value *source)
{
    if (source->flags & VAL_IS_STRING)
        fputs(source->value.string->string, target);
    else {
        lily_msgbuf *msgbuf = vm->vm_buffer;
        lily_mb_flush(msgbuf);
        lily_vm_add_value_to_msgbuf(vm, msgbuf, source);
        fputs(lily_mb_get(msgbuf), target);
    }

    fputc('\n', target);
}

void lily_builtin_print(lily_vm_state *vm)
{
    do_print(vm, stdout, lily_arg_value(vm, 0));
}

/* Initially, print is implemented through lily_builtin_print. However, when
   stdout is dynaloaded, that doesn't work. When stdout is found, print needs to
   use the register holding Lily's stdout, not the plain C stdout. */
static void builtin_stdout_print(lily_vm_state *vm)
{
    lily_file_val *stdout_val = vm->stdout_reg->value.file;
    if (stdout_val->inner_file == NULL)
        lily_error(vm, SYM_CLASS_VALUEERROR,
                "IO operation on closed file.");

    do_print(vm, stdout_val->inner_file, lily_arg_value(vm, 0));
}

void lily_return_tag_dynamic(lily_vm_state *vm, lily_dynamic_val *);

void lily_builtin_Dynamic_new(lily_vm_state *vm)
{
    lily_value *input = lily_arg_value(vm, 0);

    lily_dynamic_val *dynamic_val = lily_new_dynamic_val();
    lily_dynamic_set_value(dynamic_val, input);

    lily_value *target = vm->call_chain->prev->return_target;
    lily_move_dynamic(target, dynamic_val);
    lily_tag_value(vm, target);
}

/***
 *       ___                      _
 *      / _ \ _ __   ___ ___   __| | ___  ___
 *     | | | | '_ \ / __/ _ \ / _` |/ _ \/ __|
 *     | |_| | |_) | (_| (_) | (_| |  __/\__ \
 *      \___/| .__/ \___\___/ \__,_|\___||___/
 *           |_|
 */

/** These functions handle various opcodes for the vm. The thinking is to try to
    keep the vm exec function "small" by kicking out big things. **/

/* Internally, classes are really just tuples. So assigning them is like
   accessing a tuple, except that the index is a raw int instead of needing to
   be loaded from a register. */
static void do_o_set_property(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *rhs_reg;
    int index;
    lily_instance_val *ival;

    index = code[2];
    ival = vm_regs[code[3]]->value.instance;
    rhs_reg = vm_regs[code[4]];

    lily_assign_value(ival->values[index], rhs_reg);
}

static void do_o_get_property(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result_reg;
    int index;
    lily_instance_val *ival;

    index = code[2];
    ival = vm_regs[code[3]]->value.instance;
    result_reg = vm_regs[code[4]];

    lily_assign_value(result_reg, ival->values[index]);
}

/* This handles subscript assignment. The index is a register, and needs to be
   validated. */
static void do_o_set_item(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *lhs_reg, *index_reg, *rhs_reg;

    lhs_reg = vm_regs[code[2]];
    index_reg = vm_regs[code[3]];
    rhs_reg = vm_regs[code[4]];

    if ((lhs_reg->flags & VAL_IS_HASH) == 0) {
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

        lily_assign_value(list_val->elems[index_int], rhs_reg);
    }
    else
        lily_hash_set_elem(vm, lhs_reg->value.hash, index_reg, rhs_reg);
}

/* This handles subscript access. The index is a register, and needs to be
   validated. */
static void do_o_get_item(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *lhs_reg, *index_reg, *result_reg;

    lhs_reg = vm_regs[code[2]];
    index_reg = vm_regs[code[3]];
    result_reg = vm_regs[code[4]];

    /* list and tuple have the same representation internally. Since list
       stores proper values, lily_assign_value automagically set the type to
       the right thing. */
    if (lhs_reg->flags & VAL_IS_STRING)
        lily_string_subscript(vm, lhs_reg, index_reg, result_reg);
    else if (lhs_reg->flags & (VAL_IS_LIST | VAL_IS_TUPLE)) {
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

        lily_assign_value(result_reg, list_val->elems[index_int]);
    }
    else {
        lily_hash_elem *hash_elem = lily_hash_get_elem(vm, lhs_reg->value.hash,
                index_reg);

        /* Give up if the key doesn't exist. */
        if (hash_elem == NULL)
            key_error(vm, index_reg, code[1]);

        lily_assign_value(result_reg, hash_elem->elem_value);
    }
}

/* This builds a hash. It's written like '#pairs, key, value, key, value...'. */
static void do_o_build_hash(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int i, num_values;
    lily_value *result, *key_reg, *value_reg;

    num_values = code[2];
    result = vm_regs[code[3 + num_values]];

    lily_hash_val *hash_val = lily_new_hash_val();
    lily_move_hash_f(MOVE_DEREF_SPECULATIVE, result, hash_val);

    for (i = 0;
         i < num_values;
         i += 2) {
        key_reg = vm_regs[code[3 + i]];
        value_reg = vm_regs[code[3 + i + 1]];

        lily_hash_set_elem(vm, hash_val, key_reg, value_reg);
    }
}

/* Lists and tuples are effectively the same thing internally, since the list
   value holds proper values. This is used primarily to do as the name suggests.
   However, variant types are also tuples (but with a different name). */
static void do_o_build_list_tuple(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int num_elems = code[2];
    lily_value *result = vm_regs[code[3+num_elems]];

    lily_list_val *lv = lily_new_list_val_n(num_elems);
    lily_value **elems = lv->elems;

    if (code[0] == o_build_list)
        lily_move_list_f(MOVE_DEREF_SPECULATIVE, result, lv);
    else
        lily_move_tuple_f(MOVE_DEREF_SPECULATIVE, result, lv);

    int i;
    for (i = 0;i < num_elems;i++) {
        lily_value *rhs_reg = vm_regs[code[3+i]];
        lily_assign_value(elems[i], rhs_reg);
    }
}

static void do_o_build_enum(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int variant_id = code[2];
    int count = code[3];
    lily_value *result = vm_regs[code[code[3] + 4]];

    lily_instance_val *ival = lily_new_instance_val_n_of(count, variant_id);
    lily_value **slots = ival->values;

    lily_move_enum_f(MOVE_DEREF_SPECULATIVE, result, ival);

    int i;
    for (i = 0;i < count;i++) {
        lily_value *rhs_reg = vm_regs[code[4+i]];
        lily_assign_value(slots[i], rhs_reg);
    }
}

/* This raises a user-defined exception. The emitter has verified that the thing
   to be raised is raiseable (extends Exception). */
static void do_o_raise(lily_vm_state *vm, lily_value *exception_val)
{
    /* The Exception class has values[0] as the message, values[1] as the
       container for traceback. */

    lily_instance_val *ival = exception_val->value.instance;
    char *message = ival->values[0]->value.string->string;
    lily_class *raise_cls = vm->class_table[ival->instance_id];

    /* There's no need for a ref/deref here, because the gc cannot trigger
       foreign stack unwind and/or exception capture. */
    vm->exception_value = exception_val;
    lily_raise_class(vm->raiser, raise_cls, message);
}

/* This is an uncommon, but decently fast opcode. What it does is to scan from
   the last optional register down. The first one that has a value decides where
   to jump. If none are set, it'll fall into the last jump, which will jump
   right to the start of all the instructions.
   This is done outside of the vm's main loop because it's not common. */
static int do_o_optarg_dispatch(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    uint16_t first_spot = code[1];
    int count = code[2] - 1;
    unsigned int i;

    for (i = 0;i < count;i++) {
        lily_value *reg = vm_regs[first_spot - i];
        if (reg->flags)
            break;
    }

    return code[3 + i];
}

/* This creates a new instance of a class. This checks if the current call is
   part of a constructor chain. If so, it will attempt to use the value
   currently being built instead of making a new one.
   There are three opcodes that come in through here. This will use the incoming
   opcode as a way of deducing what to do with the newly-made instance. */
static void do_o_new_instance(lily_vm_state *vm, uint16_t *code)
{
    int total_entries;
    int cls_id = code[2];
    lily_value **vm_regs = vm->vm_regs;
    lily_value *result = vm_regs[code[3]];
    lily_class *instance_class = vm->class_table[cls_id];

    total_entries = instance_class->prop_count;

    lily_call_frame *caller_frame = vm->call_chain->prev;

    /* Check to see if the caller is in the process of building a subclass
       of this value. If that is the case, then use that instance instead of
       building one that will simply be tossed. */
    if (caller_frame->build_value) {
        lily_value *build_value = caller_frame->build_value;
        int build_id = build_value->value.instance->instance_id;

        if (build_id > cls_id) {
            /* Ensure that there is an actual lineage here. */
            int found = 0;
            lily_class *iter_class = vm->class_table[build_id]->parent;
            while (iter_class) {
                if (iter_class->id == cls_id) {
                    found = 1;
                    break;
                }

                iter_class = iter_class->parent;
            }

            if (found) {
                lily_assign_value(result, caller_frame->build_value);

                /* This causes the 'self' value to bubble upward. */
                vm->call_chain->build_value = result;
                return;
            }
        }
    }

    lily_instance_val *iv = lily_new_instance_val_n_of(total_entries, cls_id);

    if (code[0] == o_new_instance_speculative)
        lily_move_instance_f(MOVE_DEREF_SPECULATIVE, result, iv);
    else {
        lily_move_instance_f(MOVE_DEREF_NO_GC, result, iv);
        if (code[0] == o_new_instance_tagged)
            lily_tag_value(vm, result);
    }

    /* This is set so that a superclass .new can simply pull this instance,
       since this instance will have >= the # of types. */
    vm->call_chain->build_value = result;
}

static void do_o_interpolation(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    int count = code[2];
    lily_msgbuf *vm_buffer = vm->vm_buffer;
    lily_mb_flush(vm_buffer);

    int i;
    for (i = 0;i < count;i++) {
        lily_value *v = vm_regs[code[3 + i]];
        lily_vm_add_value_to_msgbuf(vm, vm_buffer, v);
    }

    lily_value *result_reg = vm_regs[code[3 + i]];

    lily_move_string(result_reg, lily_new_raw_string(lily_mb_get(vm_buffer)));
}

static void do_o_dynamic_cast(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_class *cast_class = vm->class_table[code[2]];
    lily_value *rhs_reg = vm_regs[code[3]];
    lily_value *lhs_reg = vm_regs[code[4]];

    lily_value *inner = rhs_reg->value.dynamic->inner_value;

    int ok = 0;
    if (inner->flags & (VAL_IS_INSTANCE | VAL_IS_ENUM))
        ok = (inner->value.instance->instance_id == cast_class->id);
    else
        /* Note: This won't work for foreign values, but there are none of those
           until the postgres module is fixed. */
        ok = cast_class->move_flags & inner->flags;

    if (ok) {
        lily_instance_val *variant = lily_new_some();
        lily_variant_set_value(variant, 0, inner);
        lily_move_enum_f(MOVE_DEREF_SPECULATIVE, lhs_reg, variant);
    }
    else
        lily_move_empty_variant(lhs_reg, lily_get_none(vm));
}

/***
 *       ____ _
 *      / ___| | ___  ___ _   _ _ __ ___  ___
 *     | |   | |/ _ \/ __| | | | '__/ _ \/ __|
 *     | |___| | (_) \__ \ |_| | | |  __/\__ \
 *      \____|_|\___/|___/\__,_|_|  \___||___/
 *
 */

/** Closures are pretty easy in the vm. It helps that the emitter has written
    'mirroring' get/set upvalue instructions around closed values. That means
    that the closure's information will always be up-to-date.

    Closures in the vm work by first creating a shallow copy of a given function
    value. The closure will then create an area for closure values. These
    values are termed the closure's cells.

    A closure is permitted to share cells with another closure. A cell will be
    destroyed when it has a zero for the cell refcount. This prevents the value
    from being destroyed too early.

    Each opcode that initializes closure data is responsible for returning the
    cells that it made. This returned data is used by lily_vm_execute to do
    closure get/set but without having to fetch the closure each time. It's not
    strictly necessary, but it's a performance boost. **/

/* This takes a value and makes a closure cell that is a copy of that value. The
   value is given a ref increase. */
static lily_value *make_cell_from(lily_value *value)
{
    lily_value *result = lily_malloc(sizeof(lily_value));
    *result = *value;
    result->cell_refcount = 1;
    if (value->flags & VAL_IS_DEREFABLE)
        value->value.generic->refcount++;

    return result;
}

/* This clones the data inside of 'to_copy'. */
static lily_function_val *new_function_copy(lily_function_val *to_copy)
{
    lily_function_val *f = lily_malloc(sizeof(lily_function_val));

    *f = *to_copy;
    f->refcount = 0;

    return f;
}

/* This opcode is the bottom level of closure creation. It is responsible for
   creating the original closure. */
static lily_value **do_o_create_closure(lily_vm_state *vm, uint16_t *code)
{
    int count = code[2];
    lily_value *result = vm->vm_regs[code[3]];

    lily_function_val *last_call = vm->call_chain->function;

    lily_function_val *closure_func = new_function_copy(last_call);

    lily_value **upvalues = lily_malloc(sizeof(lily_value *) * count);

    /* Cells are initially NULL so that o_set_upvalue knows to copy a new value
       into a cell. */
    int i;
    for (i = 0;i < count;i++)
        upvalues[i] = NULL;

    closure_func->num_upvalues = count;
    closure_func->upvalues = upvalues;

    lily_move_function_f(MOVE_DEREF_NO_GC, result, closure_func);
    lily_tag_value(vm, result);

    return upvalues;
}

/* This copies cells from 'source' to 'target'. Cells that exist are given a
   cell_refcount bump. */
static void copy_upvalues(lily_function_val *target, lily_function_val *source)
{
    lily_value **source_upvalues = source->upvalues;
    int count = source->num_upvalues;

    lily_value **new_upvalues = lily_malloc(sizeof(lily_value *) * count);
    lily_value *up;
    int i;

    for (i = 0;i < count;i++) {
        up = source_upvalues[i];
        if (up)
            up->cell_refcount++;

        new_upvalues[i] = up;
    }

    target->upvalues = new_upvalues;
    target->num_upvalues = count;
}

/* This opcode will create a copy of a given function that pulls upvalues from
   the specified closure. */
static void do_o_create_function(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->vm_regs;
    lily_value *input_closure_reg = vm_regs[code[1]];

    lily_value *target = vm->readonly_table[code[2]];
    lily_function_val *target_func = target->value.function;

    lily_value *result_reg = vm_regs[code[3]];
    lily_function_val *new_closure = new_function_copy(target_func);

    copy_upvalues(new_closure, input_closure_reg->value.function);

    lily_move_function_f(MOVE_DEREF_SPECULATIVE, result_reg, new_closure);
    lily_tag_value(vm, result_reg);
}

/* This is written at the top of a define that uses a closure (unless that
   define is a class method).

   This instruction is unique in that there's a particular problem that needs to
   be addressed. If function 'f' is a closure and is recursively called, there
   will be existing cells at the level of 'f'. Naturally, this will lead to the
   cells at that level being rewritten.

   Would you expect calling a function recursively to modify local values in the
   current frame? Almost certainly not! This solves that problem by including
   the spots in the closure at the level of 'f'. These spots are deref'd and
   NULL'd, so that any recursive call does not damage locals. */
static lily_value **do_o_load_closure(lily_vm_state *vm, uint16_t *code)
{
    lily_function_val *input_closure = vm->call_chain->function;

    lily_value **upvalues = input_closure->upvalues;
    int count = code[2];
    int i;
    lily_value *up;

    code = code + 3;

    for (i = 0;i < count;i++) {
        up = upvalues[code[i]];
        if (up) {
            up->cell_refcount--;
            if (up->cell_refcount == 0) {
                lily_deref(up);
                lily_free(up);
            }

            upvalues[code[i]] = NULL;
        }
    }

    lily_value *result_reg = vm->vm_regs[code[i]];

    input_closure->refcount++;

    /* Closures are always tagged. Do this as a custom move, because this is,
       so far, the only scenario where a move needs to mark a tagged value. */
    lily_move_function_f(VAL_IS_DEREFABLE | VAL_IS_GC_TAGGED, result_reg,
            input_closure);

    return input_closure->upvalues;
}

/* This handles when a class method is a closure. Class methods will pull
   closure information from a special *closure property in the class. Doing it
   that way allows class methods to be used statically with ease regardless of
   if they are a closure. */
static lily_value **do_o_load_class_closure(lily_vm_state *vm, uint16_t *code)
{
    do_o_get_property(vm, code);
    lily_value *result_reg = vm->vm_regs[code[4]];
    lily_function_val *input_closure = result_reg->value.function;

    lily_function_val *new_closure = new_function_copy(input_closure);
    copy_upvalues(new_closure, input_closure);

    lily_move_function_f(MOVE_DEREF_SPECULATIVE, result_reg, new_closure);

    return new_closure->upvalues;
}

/***
 *      _____                    _   _
 *     | ____|_  _____ ___ _ __ | |_(_) ___  _ __  ___
 *     |  _| \ \/ / __/ _ \ '_ \| __| |/ _ \| '_ \/ __|
 *     | |___ >  < (_|  __/ |_) | |_| | (_) | | | \__ \
 *     |_____/_/\_\___\___| .__/ \__|_|\___/|_| |_|___/
 *                        |_|
 */

/** Exception capture is a small but important part of the vm. Exception
    capturing can be thought of as two parts: One, trying to build trace, and
    two, trying to catch the exception. The first of those is relatively easy.

    Actually capturing exceptions is a little rough though. The interpreter
    currently allows raising a code that the vm's exception capture later has to
    possibly dynaload (eww). **/

/* This builds the current exception traceback into a raw list value. It is up
   to the caller to move the raw list to somewhere useful. */
static lily_list_val *build_traceback_raw(lily_vm_state *vm)
{
    lily_list_val *lv = lily_new_list_val_n(vm->call_depth);
    lily_call_frame *frame_iter;

    int i;

    /* The call chain goes from the most recent to least. Work around that by
       allocating elements in reverse order. It's safe to do this because
       nothing in this loop can trigger the gc. */
    for (i = vm->call_depth, frame_iter = vm->call_chain;
         i >= 1;
         i--, frame_iter = frame_iter->prev) {
        lily_function_val *func_val = frame_iter->function;
        char *path;
        char line[16] = "";
        const char *class_name;
        char *separator;
        const char *name = func_val->trace_name;
        if (func_val->code) {
            path = func_val->module->path;
            sprintf(line, "%d:", frame_iter->line_num);
        }
        else
            path = "[C]";

        if (func_val->class_name == NULL) {
            class_name = "";
            separator = "";
        }
        else {
            separator = ".";
            class_name = func_val->class_name;
        }

        /* +9 accounts for the non-format part, and a terminator. */
        int str_size = strlen(path) + strlen(line) + strlen(class_name) +
                strlen(name) + strlen(separator) + 9;

        char *str = lily_malloc(str_size);
        sprintf(str, "%s:%s from %s%s%s", path, line, class_name, separator,
                name);

        lily_move_string(lv->elems[i - 1], lily_new_raw_string_take(str));
    }

    return lv;
}

/* This is called when a builtin exception has been thrown. All builtin
   exceptions are subclasses of Exception with only a traceback and message
   field being set. This builds a new value of the given type with the message
   and newly-made traceback. */
static void make_proper_exception_val(lily_vm_state *vm,
        lily_class *raised_cls, lily_value *result)
{
    const char *raw_message = lily_mb_get(vm->raiser->msgbuf);
    lily_instance_val *ival = lily_new_instance_val_n_of(2, raised_cls->id);
    lily_string_val *message = lily_new_raw_string(raw_message);
    lily_mb_flush(vm->raiser->msgbuf);

    lily_instance_set_string(ival, 0, message);
    lily_instance_set_list(ival, 1, build_traceback_raw(vm));

    lily_move_instance_f(MOVE_DEREF_SPECULATIVE, result, ival);
}

/* This is called when 'raise' raises an error. The traceback property is
   assigned to freshly-made traceback. The other fields of the value are left
   intact, however. */
static void fixup_exception_val(lily_vm_state *vm, lily_value *result)
{
    lily_assign_value(result, vm->exception_value);
    lily_list_val *raw_trace = build_traceback_raw(vm);
    lily_instance_val *iv = result->value.instance;

    lily_move_list_f(MOVE_DEREF_SPECULATIVE, iv->values[1], raw_trace);
}

/* This attempts to catch the exception that the raiser currently holds. If it
   succeeds, then the vm's state is updated and the exception is cleared out.

   This function will refuse to catch exceptions that are not on the current
   internal lily_vm_execute depth. This is so that the vm will return out. To do
   otherwise leaves the vm thinking it is N levels deep but being, say, N - 2
   levels deep.

   Returns 1 if the exception has been caught, 0 otherwise. */
static int maybe_catch_exception(lily_vm_state *vm)
{
    lily_class *raised_cls = vm->raiser->exception_cls;

    /* The catch entry pointer is always one spot ahead of the last entry that
       was inserted. So this is safe. */
    if (vm->catch_chain->prev == NULL)
        return 0;

    lily_jump_link *raiser_jump = vm->raiser->all_jumps;

    lily_vm_catch_entry *catch_iter = vm->catch_chain->prev;
    lily_value *catch_reg = NULL;
    lily_value **stack_regs;
    int do_unbox, jump_location, match;

    match = 0;

    while (catch_iter != NULL) {
        /* It's extremely important that the vm not attempt to catch exceptions
           that were not made in the same jump level. If it does, the vm could
           be called from a foreign function, but think it isn't. */
        if (catch_iter->jump_entry != raiser_jump) {
            vm->catch_chain = catch_iter->next;
            break;
        }

        lily_call_frame *call_frame = catch_iter->call_frame;
        uint16_t *code = call_frame->function->code;
        /* A try block is done when the next jump is at 0 (because 0 would
           always be going back, which is illogical otherwise). */
        jump_location = catch_iter->code_pos + code[catch_iter->code_pos] - 2;
        stack_regs = vm->regs_from_main + catch_iter->offset_from_main;

        while (1) {
            lily_class *catch_class = vm->class_table[code[jump_location + 2]];

            if (lily_class_greater_eq(catch_class, raised_cls)) {
                /* There are two exception opcodes:
                 * o_except_catch will have #4 as a valid register, and is
                   interested in having that register filled with data later on.
                 * o_except_ignore doesn't care, so #4 is always 0. Having it as
                   zero allows catch_reg do not need a condition check, since
                   stack_regs[0] is always safe. */
                do_unbox = code[jump_location] == o_except_catch;

                catch_reg = stack_regs[code[jump_location + 3]];

                /* ...So that execution resumes from within the except block. */
                jump_location += 5;
                match = 1;
                break;
            }
            else {
                int move_by = code[jump_location + 4];
                if (move_by == 0)
                    break;

                jump_location += move_by;
            }
        }

        if (match)
            break;

        catch_iter = catch_iter->prev;
    }

    if (match) {
        if (do_unbox) {
            /* There is a var that the exception needs to be dropped into. If
               this exception was triggered by raise, then use that (after
               dumping traceback into it). If not, create a new instance to
               hold the info. */
            if (vm->exception_value)
                fixup_exception_val(vm, catch_reg);
            else
                make_proper_exception_val(vm, raised_cls, catch_reg);
        }

        /* Make sure any exception value that was held is gone. No ref/deref is
           necessary, because the value was saved somewhere in a register. */
        vm->exception_value = NULL;
        vm->call_chain = catch_iter->call_frame;
        vm->call_depth = catch_iter->call_frame_depth;
        vm->vm_regs = stack_regs;
        vm->call_chain->code = vm->call_chain->function->code + jump_location;
        /* Each try block can only successfully handle one exception, so use
           ->prev to prevent using the same block again. */
        vm->catch_chain = catch_iter;
    }

    return match;
}

/***
 *      _____              _                  _    ____ ___
 *     |  ___|__  _ __ ___(_) __ _ _ __      / \  |  _ \_ _|
 *     | |_ / _ \| '__/ _ \ |/ _` | '_ \    / _ \ | |_) | |
 *     |  _| (_) | | |  __/ | (_| | | | |  / ___ \|  __/| |
 *     |_|  \___/|_|  \___|_|\__, |_| |_| /_/   \_\_|  |___|
 *                           |___/
 */

lily_msgbuf *lily_get_msgbuf(lily_vm_state *vm)
{
    lily_msgbuf *msgbuf = vm->vm_buffer;
    /* Almost every caller wants a fresh buffer, so do that for them. */
    lily_mb_flush(msgbuf);
    return msgbuf;
}

lily_msgbuf *lily_get_msgbuf_noflush(lily_vm_state *vm)
{
    return vm->vm_buffer;
}

uint16_t *lily_get_cid_table(lily_vm_state *vm)
{
    return vm->call_chain->function->cid_table;
}

/** Foreign functions that are looking to interact with the interpreter can use
    the functions within here. Do be careful with foreign calls, however. **/

void lily_prepare_call(lily_vm_state *vm, lily_function_val *func)
{
    lily_call_frame *caller_frame = vm->call_chain;
    caller_frame->code = foreign_code;
    caller_frame->return_target = vm->vm_regs[caller_frame->regs_used];

    if (caller_frame->next == NULL) {
        add_call_frame(vm);
        /* The vm's call chain automatically advances when add_call_frame is
            used. That's useful for the vm, but not here. Rewind the frame
            back so that every invocation of this call will have the same
            call_chain. */
        vm->call_chain = caller_frame;
    }

    lily_call_frame *target_frame = caller_frame->next;
    target_frame->code = func->code;
    target_frame->function = func;
    target_frame->line_num = 0;
    target_frame->regs_used = func->reg_count;
}

void lily_exec_prepared(lily_vm_state *vm, int count)
{
    lily_call_frame *target_frame = vm->call_chain->next;
    lily_function_val *target_fn = target_frame->function;

    vm->call_depth++;

    if (target_fn->code == NULL) {
        target_frame->regs_used = count;

        vm->vm_regs = vm->regs_from_main + vm->num_registers - count;
        vm->call_chain = target_frame;

        target_fn->foreign_func(vm);

        vm->call_chain = target_frame->prev;
        vm->num_registers -= count;

        vm->call_depth--;
    }
    else {
        int save = vm->num_registers;
        int need = vm->num_registers + target_fn->reg_count;
        int distance = target_fn->reg_count - count;

        if (need > vm->max_registers)
            grow_vm_registers(vm, need);

        vm->vm_regs = vm->regs_from_main + vm->num_registers - count;
        vm->call_chain = target_frame;

        if (distance > 0) {
            /* The register count is one ahead so that pushes don't need to add.
               Drop it back since register scrub relies on it.
               Without this, register 1 won't be cleared. */
            vm->num_registers--;
            scrub_registers(vm, target_fn, count);
            vm->num_registers++;
        }

        /* Increase the register count to include the intermediates that will be
           needed by the native function. */
        vm->num_registers += distance;

        lily_vm_execute(vm);

        /* The frame is dropped when returning from native execute.
           Leave the call chain and the depth alone. */

        /* Drop the registers back to what they were before the call. */
        vm->num_registers = save - count;
        vm->vm_regs = vm->regs_from_main + target_frame->prev->offset_to_main;
    }
}

void lily_exec_simple(lily_vm_state *vm, lily_function_val *f, int count)
{
    lily_prepare_call(vm, f);
    lily_exec_prepared(vm, count);
}

/* This calculates a siphash for a given hash value. The siphash is based off of
   the vm's sipkey. The caller is expected to only call this for keys that are
   hashable. */
uint64_t lily_siphash(lily_vm_state *vm, lily_value *key)
{
    int flags = key->flags;
    uint64_t key_hash;

    if (flags & VAL_IS_STRING)
        key_hash = siphash24(key->value.string->string,
                key->value.string->size, vm->sipkey);
    else if (flags & VAL_IS_INTEGER)
        key_hash = key->value.integer;
    else /* Should not happen, because no other classes are valid keys. */
        key_hash = 0;

    return key_hash;
}

typedef struct tag_ {
    struct tag_ *prev;
    lily_raw_value raw;
} tag;

static void add_value_to_msgbuf(lily_vm_state *, lily_msgbuf *, tag *,
        lily_value *);

static void add_list_like(lily_vm_state *vm, lily_msgbuf *msgbuf, tag *t,
        lily_value *v, const char *prefix, const char *suffix)
{
    int count, i;
    lily_value **values;

    if (v->flags & (VAL_IS_LIST | VAL_IS_TUPLE)) {
        values = v->value.list->elems;
        count = v->value.list->num_values;
    }
    else {
        values = v->value.instance->values;
        count = v->value.instance->num_values;
    }

    lily_mb_add(msgbuf, prefix);

    /* This is necessary because num_values is unsigned. */
    if (count != 0) {
        for (i = 0;i < count - 1;i++) {
            add_value_to_msgbuf(vm, msgbuf, t, values[i]);
            lily_mb_add(msgbuf, ", ");
        }
        if (i != count)
            add_value_to_msgbuf(vm, msgbuf, t, values[i]);
    }

    lily_mb_add(msgbuf, suffix);
}

static void add_value_to_msgbuf(lily_vm_state *vm, lily_msgbuf *msgbuf,
        tag *t, lily_value *v)
{
    if (v->flags & VAL_IS_GC_TAGGED) {
        tag *tag_iter = t;
        while (tag_iter) {
            /* Different containers may hold the same underlying values, so make
               sure to NOT test the containers. */
            if (memcmp(&tag_iter->raw, &v->value, sizeof(lily_raw_value)) == 0) {
                lily_mb_add(msgbuf, "[...]");
                return;
            }

            tag_iter = tag_iter->prev;
        }

        tag new_tag = {.prev = t, .raw = v->value};
        t = &new_tag;
    }

    if (v->flags & VAL_IS_BOOLEAN)
        lily_mb_add_boolean(msgbuf, v->value.integer);
    else if (v->flags & VAL_IS_INTEGER)
        lily_mb_add_int(msgbuf, v->value.integer);
    else if (v->flags & VAL_IS_DOUBLE)
        lily_mb_add_double(msgbuf, v->value.doubleval);
    else if (v->flags & VAL_IS_STRING)
        lily_mb_add_fmt(msgbuf, "\"^E\"", v->value.string->string);
    else if (v->flags & VAL_IS_BYTESTRING)
        lily_mb_add_bytestring(msgbuf, v->value.string->string,
                v->value.string->size);
    else if (v->flags & VAL_IS_FUNCTION) {
        lily_function_val *fv = v->value.function;
        const char *builtin = "";
        const char *class_name = "";
        const char *separator = "";

        if (fv->code == NULL)
            builtin = "built-in ";

        if (fv->class_name) {
            class_name = fv->class_name;
            separator = ".";
        }

        lily_mb_add_fmt(msgbuf, "<%sfunction %s%s%s>", builtin, class_name,
                separator, fv->trace_name);
    }
    else if (v->flags & VAL_IS_DYNAMIC)
        add_value_to_msgbuf(vm, msgbuf, t, v->value.dynamic->inner_value);
    else if (v->flags & VAL_IS_LIST)
        add_list_like(vm, msgbuf, t, v, "[", "]");
    else if (v->flags & VAL_IS_TUPLE)
        add_list_like(vm, msgbuf, t, v, "<[", "]>");
    else if (v->flags & VAL_IS_HASH) {
        lily_hash_val *hv = v->value.hash;
        lily_mb_add_char(msgbuf, '[');
        lily_hash_elem *elem = hv->elem_chain;
        while (elem) {
            add_value_to_msgbuf(vm, msgbuf, t, elem->elem_key);
            lily_mb_add(msgbuf, " => ");
            add_value_to_msgbuf(vm, msgbuf, t, elem->elem_value);
            if (elem->next != NULL)
                lily_mb_add(msgbuf, ", ");

            elem = elem->next;
        }
        lily_mb_add_char(msgbuf, ']');
    }
    else if (v->flags & VAL_IS_FILE) {
        lily_file_val *fv = v->value.file;
        const char *state = fv->inner_file ? "open" : "closed";
        lily_mb_add_fmt(msgbuf, "<%s file at %p>", state, fv);
    }
    else if (v->flags & VAL_IS_ENUM) {
        lily_instance_val *variant = v->value.instance;
        lily_class *variant_cls = vm->class_table[variant->instance_id];

        /* For scoped variants, render them how they're written. */
        if (variant_cls->parent->flags & CLS_ENUM_IS_SCOPED) {
            lily_mb_add(msgbuf, variant_cls->parent->name);
            lily_mb_add_char(msgbuf, '.');
        }

        lily_mb_add(msgbuf, variant_cls->name);
        if (variant->num_values)
            add_list_like(vm, msgbuf, t, v, "(", ")");
    }
    else {
        /* This is an instance or a foreign class. The instance id is at the
           same spot for both. */
        lily_class *cls = vm->class_table[v->value.instance->instance_id];
        const char *package_name = "";
        const char *separator = "";

        lily_mb_add_fmt(msgbuf, "<%s%s%s at %p>", package_name, separator,
                cls->name, v->value.generic);
    }
}

void lily_vm_add_value_to_msgbuf(lily_vm_state *vm, lily_msgbuf *msgbuf,
        lily_value *value)
{
    /* The thinking is that a String that is not within anything should be added
       as-is. However, Strings that are contained within, say, a List or a
       variant should be escaped and have quoted printed around them. */
    if (value->flags & VAL_IS_STRING)
        lily_mb_add(msgbuf, value->value.string->string);
    else
        add_value_to_msgbuf(vm, msgbuf, NULL, value);
}

/***
 *      ____
 *     |  _ \ _ __ ___ _ __
 *     | |_) | '__/ _ \ '_ \
 *     |  __/| | |  __/ |_) |
 *     |_|   |_|  \___| .__/
 *                    |_|
 */

/** These functions are concerned with preparing lily_vm_execute to be called
    after reading a script in. Lily stores both defined functions and literal
    values in a giant array so that they can be accessed by index later.
    However, to save memory, it holds them as a linked list during parsing so
    that it doesn't aggressively over or under allocate array space. Now that
    parsing is done, the linked list is mapped over to the array.

    During non-tagged execute, this should happen only once. In tagged mode, it
    happens for each closing ?> tag. **/

void lily_vm_ensure_class_table(lily_vm_state *vm, int size)
{
    int old_count = vm->class_count;

    if (size >= vm->class_count) {
        if (vm->class_count == 0)
            vm->class_count = 1;

        while (size >= vm->class_count)
            vm->class_count *= 2;

        vm->class_table = lily_realloc(vm->class_table,
                sizeof(lily_class *) * vm->class_count);
    }

    /* For the first pass, make sure the spots for Exception and its built-in
       children are zero'ed out. This allows lily_error to safely check if an
       exception class has been loaded by testing the class field for being NULL
       (and relies on holes being set aside for these exceptions). */
    if (old_count == 0) {
        int i;
        for (i = SYM_CLASS_EXCEPTION;i < START_CLASS_ID;i++)
            vm->class_table[i] = NULL;
    }
}

void lily_vm_add_class_unchecked(lily_vm_state *vm, lily_class *cls)
{
    vm->class_table[cls->id] = cls;
}

void lily_vm_add_class(lily_vm_state *vm, lily_class *cls)
{
    lily_vm_ensure_class_table(vm, cls->id + 1);
    vm->class_table[cls->id] = cls;
}

/* Foreign values are created when Lily needs to dynaload a var. This receives
   those values now that vm has the registers allocated. */
static void load_foreign_values(lily_vm_state *vm, lily_value_stack *values)
{
    while (lily_vs_pos(values)) {
        lily_foreign_value *fv = (lily_foreign_value *)lily_vs_pop(values);
        uint16_t reg_spot = fv->reg_spot;

        /* The value already has a ref from being made, so don't use regular
           assign or it will have two refs. Since this is a transfer of
           ownership, use noref and drop the old container. */
        lily_assign_value_noref(vm->regs_from_main[reg_spot], (lily_value *)fv);
        lily_free(fv);
    }
}

static void maybe_fix_print(lily_vm_state *vm)
{
    lily_symtab *symtab = vm->symtab;
    lily_module_entry *builtin = symtab->builtin_module;
    lily_var *stdout_var = lily_find_var(symtab, builtin, "stdout");

    if (stdout_var) {
        lily_var *print_var = lily_find_var(symtab, builtin, "print");
        if (print_var) {
            /* Normally, the implementation of print will shoot directly to
               raw stdout. It's really fast because it doesn't have to load
               stdout from a register, and doesn't have to check for stdout
               maybe being closed.
               Now that stdout has been dynaloaded, swap the underlying function
               for print to the safe one. */
            lily_value *print_value = vm->readonly_table[print_var->reg_spot];
            print_value->value.function->foreign_func = builtin_stdout_print;
            lily_value *stdout_reg = vm->regs_from_main[stdout_var->reg_spot];
            vm->stdout_reg = stdout_reg;
        }
    }
}

/* This must be called before lily_vm_execute if the parser has read any data
   in. This makes sure that __main__ has enough register slots, that the
   vm->readonly_table is set, and that foreign ties are loaded. */
void lily_vm_prep(lily_vm_state *vm, lily_symtab *symtab,
        lily_value **readonly_table, lily_value_stack *foreign_values)
{
    vm->readonly_table = readonly_table;

    lily_function_val *main_function = symtab->main_function;

    if (main_function->reg_count > vm->max_registers) {
        grow_vm_registers(vm, main_function->reg_count);
        /* grow_vm_registers will move vm->vm_regs (which is supposed to be
           local). That works everywhere...but here. Fix vm_regs back to the
           start because we're still in __main__. */
        vm->vm_regs = vm->regs_from_main;
    }
    else if (vm->vm_regs == NULL) {
        /* This forces vm->vm_regs and vm->regs_from_main to not be NULL. This
           prevents a segfault that happens when __main__ calls something that
           does not have a register need (ex: __import__) and tries to calculate
           the return register. Without this, vm->vm_regs will be NULL and not
           get resized, resulting in a crash. */
        grow_vm_registers(vm, 1);
        vm->vm_regs = vm->regs_from_main;
    }

    load_foreign_values(vm, foreign_values);

    if (vm->stdout_reg == NULL)
        maybe_fix_print(vm);

    vm->num_registers = main_function->reg_count;

    lily_call_frame *first_frame = vm->call_chain;
    first_frame->function = main_function;
    first_frame->code = main_function->code;
    first_frame->regs_used = main_function->reg_count;
    first_frame->return_target = NULL;
    first_frame->build_value = NULL;
    vm->call_depth = 1;
}

/***
 *      _____                     _
 *     | ____|_  _____  ___ _   _| |_ ___
 *     |  _| \ \/ / _ \/ __| | | | __/ _ \
 *     | |___ >  <  __/ (__| |_| | ||  __/
 *     |_____/_/\_\___|\___|\__,_|\__\___|
 *
 */

void lily_vm_execute(lily_vm_state *vm)
{
    uint16_t *code;
    lily_value **regs_from_main;
    lily_value **vm_regs;
    int i, num_registers, max_registers;
    register int64_t for_temp;
    register lily_value *lhs_reg, *rhs_reg, *loop_reg, *step_reg;
    lily_function_val *fval;
    lily_value **upvalues = NULL;

    lily_call_frame *current_frame = vm->call_chain;
    code = current_frame->function->code;

    /* Initialize local vars from the vm state's vars. */
    vm_regs = vm->vm_regs;
    regs_from_main = vm->regs_from_main;
    max_registers = vm->max_registers;

    lily_jump_link *link = lily_jump_setup(vm->raiser);
    if (setjmp(link->jump) != 0) {
        /* If the current function is a native one, then fix the line
           number of it. Otherwise, leave the line number alone. */
        if (vm->call_chain->function->code != NULL) {
            if (vm->pending_line) {
                current_frame->line_num = vm->pending_line;
                vm->pending_line = 0;
            }
            else
                vm->call_chain->line_num = vm->call_chain->code[1];
        }

        if (maybe_catch_exception(vm) == 0)
            /* Couldn't catch it. Jump back into parser, which will jump
               back to the caller to give them the bad news. */
            lily_jump_back(vm->raiser);
        else {
            /* The exception was caught, so resync local data. */
            current_frame = vm->call_chain;
            code = current_frame->code;
            upvalues = current_frame->upvalues;
            regs_from_main = vm->regs_from_main;
            vm_regs = vm->vm_regs;
            vm->num_registers = (vm_regs - regs_from_main) + current_frame->regs_used;
        }
    }

    num_registers = vm->num_registers;

    while (1) {
        switch(code[0]) {
            case o_fast_assign:
                rhs_reg = vm_regs[code[2]];
                lhs_reg = vm_regs[code[3]];
                lhs_reg->flags = rhs_reg->flags;
                lhs_reg->value = rhs_reg->value;
                code += 4;
                break;
            case o_get_readonly:
                rhs_reg = vm->readonly_table[code[2]];
                lhs_reg = vm_regs[code[3]];

                lily_deref(lhs_reg);

                lhs_reg->value = rhs_reg->value;
                lhs_reg->flags = rhs_reg->flags;
                code += 4;
                break;
            case o_get_integer:
                lhs_reg = vm_regs[code[3]];
                lhs_reg->value.integer = (int16_t)code[2];
                lhs_reg->flags = VAL_IS_INTEGER;
                code += 4;
                break;
            case o_get_boolean:
                lhs_reg = vm_regs[code[3]];
                lhs_reg->value.integer = code[2];
                lhs_reg->flags = VAL_IS_BOOLEAN;
                code += 4;
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
                code += (int16_t)code[1];
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
                rhs_reg = vm_regs[code[3]];
                if (rhs_reg->value.integer == 0)
                    lily_error(vm, SYM_CLASS_DBZERROR,
                            "Attempt to divide by zero.");
                INTEGER_OP(/)
                break;
            case o_modulo:
                /* x % 0 will do the same thing as x / 0... */
                rhs_reg = vm_regs[code[3]];
                if (rhs_reg->value.integer == 0)
                    lily_error(vm, SYM_CLASS_DBZERROR,
                            "Attempt to divide by zero.");
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
                rhs_reg = vm_regs[code[3]];
                if (rhs_reg->flags & VAL_IS_INTEGER &&
                    rhs_reg->value.integer == 0)
                    lily_error(vm, SYM_CLASS_DBZERROR,
                            "Attempt to divide by zero.");
                else if (rhs_reg->flags & VAL_IS_DOUBLE &&
                         rhs_reg->value.doubleval == 0)
                    lily_error(vm, SYM_CLASS_DBZERROR,
                            "Attempt to divide by zero.");

                INTDBL_OP(/)
                break;
            case o_jump_if:
                lhs_reg = vm_regs[code[2]];
                {
                    int flags = lhs_reg->flags;
                    int result;

                    if (flags & (VAL_IS_INTEGER | VAL_IS_BOOLEAN))
                        result = (lhs_reg->value.integer == 0);
                    else if (flags & VAL_IS_STRING)
                        result = (lhs_reg->value.string->size == 0);
                    else if (flags & VAL_IS_LIST)
                        result = (lhs_reg->value.list->num_values == 0);
                    else
                        result = 1;

                    if (result != code[1])
                        code += (int16_t)code[3];
                    else
                        code += 4;
                }
                break;
            case o_foreign_call:
                fval = vm->readonly_table[code[2]]->value.function;

                foreign_func_body: ;

                if (current_frame->next == NULL) {
                    if (vm->call_depth > 100)
                        lily_error(vm, SYM_CLASS_RUNTIMEERROR,
                                "Function call recursion limit reached.");
                    add_call_frame(vm);
                }

                i = code[3];
                current_frame->line_num = code[1];
                current_frame->code = code + i + 5;
                current_frame->upvalues = upvalues;

                int register_need = num_registers + fval->reg_count;

                if (register_need > max_registers) {
                    grow_vm_registers(vm, register_need);
                    /* Don't forget to update local info... */
                    regs_from_main       = vm->regs_from_main;
                    vm_regs              = vm->vm_regs;
                    max_registers        = vm->max_registers;
                }
                lily_foreign_func func = fval->foreign_func;

                /* Prepare the registers for what the function wants. Afterward,
                   update num_registers since prep_registers changes it. */
                prep_registers(vm, fval, code);
                current_frame->return_target = vm_regs[code[4]];
                vm_regs = vm_regs + current_frame->regs_used;
                vm->vm_regs = vm_regs;

                /* !PAST HERE TARGETS THE NEW FRAME! */

                current_frame = current_frame->next;
                vm->call_chain = current_frame;

                current_frame->offset_to_main = num_registers;
                current_frame->function = fval;
                current_frame->line_num = -1;
                current_frame->code = NULL;
                current_frame->build_value = NULL;
                current_frame->upvalues = NULL;
                current_frame->regs_used = i;

                vm->call_depth++;
                func(vm);

                /* This function may have called the vm, thus growing the number
                   of registers. Copy over important data if that's happened. */
                if (vm->max_registers != max_registers) {
                    regs_from_main = vm->regs_from_main;
                    max_registers  = vm->max_registers;
                }

                current_frame = current_frame->prev;

                vm_regs = vm->regs_from_main + num_registers - current_frame->regs_used;
                vm->vm_regs = vm_regs;

                vm->call_chain = current_frame;

                code += 5 + i;
                vm->call_depth--;
                vm->num_registers = num_registers;

                break;
            case o_native_call: {
                fval = vm->readonly_table[code[2]]->value.function;

                native_func_body: ;

                if (current_frame->next == NULL) {
                    if (vm->call_depth > 100)
                        lily_error(vm, SYM_CLASS_RUNTIMEERROR,
                                "Function call recursion limit reached.");
                    add_call_frame(vm);
                }

                i = code[3];
                current_frame->line_num = code[1];
                current_frame->code = code + i + 5;
                current_frame->upvalues = upvalues;

                int register_need = fval->reg_count + num_registers;

                if (register_need > max_registers) {
                    grow_vm_registers(vm, register_need);
                    /* Don't forget to update local info... */
                    regs_from_main = vm->regs_from_main;
                    vm_regs        = vm->vm_regs;
                    max_registers  = vm->max_registers;
                }

                /* Prepare the registers for what the function wants. Afterward,
                   update num_registers since prep_registers changes it. */
                prep_registers(vm, fval, code);
                num_registers = vm->num_registers;

                current_frame->return_target = vm_regs[code[4]];
                vm_regs = vm_regs + current_frame->regs_used;
                vm->vm_regs = vm_regs;

                /* !PAST HERE TARGETS THE NEW FRAME! */

                current_frame = current_frame->next;
                vm->call_chain = current_frame;

                current_frame->function = fval;
                current_frame->regs_used = fval->reg_count;
                current_frame->code = fval->code;
                current_frame->upvalues = NULL;
                vm->call_depth++;
                code = fval->code;
                upvalues = NULL;

                break;
            }
            case o_function_call:
                fval = vm_regs[code[2]]->value.function;

                if (fval->code != NULL)
                    goto native_func_body;
                else
                    goto foreign_func_body;

                break;
            case o_interpolation:
                do_o_interpolation(vm, code);
                code += code[2] + 4;
                break;
            case o_unary_not:
                lhs_reg = vm_regs[code[2]];

                rhs_reg = vm_regs[code[3]];
                rhs_reg->flags = lhs_reg->flags;
                rhs_reg->value.integer = !(lhs_reg->value.integer);
                code += 4;
                break;
            case o_unary_minus:
                lhs_reg = vm_regs[code[2]];

                rhs_reg = vm_regs[code[3]];
                rhs_reg->flags = VAL_IS_INTEGER;
                rhs_reg->value.integer = -(lhs_reg->value.integer);
                code += 4;
                break;
            case o_return_val:
                lhs_reg = current_frame->prev->return_target;
                rhs_reg = vm_regs[code[2]];
                lily_assign_value(lhs_reg, rhs_reg);

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
                break;
            case o_get_global:
                rhs_reg = regs_from_main[code[2]];
                lhs_reg = vm_regs[code[3]];

                lily_assign_value(lhs_reg, rhs_reg);
                code += 4;
                break;
            case o_set_global:
                rhs_reg = vm_regs[code[2]];
                lhs_reg = regs_from_main[code[3]];

                lily_assign_value(lhs_reg, rhs_reg);
                code += 4;
                break;
            case o_assign:
                rhs_reg = vm_regs[code[2]];
                lhs_reg = vm_regs[code[3]];

                lily_assign_value(lhs_reg, rhs_reg);
                code += 4;
                break;
            case o_get_item:
                do_o_get_item(vm, code);
                code += 5;
                break;
            case o_get_property:
                do_o_get_property(vm, code);
                code += 5;
                break;
            case o_set_item:
                do_o_set_item(vm, code);
                code += 5;
                break;
            case o_set_property:
                do_o_set_property(vm, code);
                code += 5;
                break;
            case o_build_hash:
                do_o_build_hash(vm, code);
                code += code[2] + 4;
                break;
            case o_build_list:
            case o_build_tuple:
                do_o_build_list_tuple(vm, code);
                code += code[2] + 4;
                break;
            case o_build_enum:
                do_o_build_enum(vm, code);
                code += code[3] + 5;
                break;
            case o_dynamic_cast:
                do_o_dynamic_cast(vm, code);
                code += 5;
                break;
            case o_create_function:
                do_o_create_function(vm, code);
                code += 4;
                break;
            case o_set_upvalue:
                lhs_reg = upvalues[code[2]];
                rhs_reg = vm_regs[code[3]];
                if (lhs_reg == NULL)
                    upvalues[code[2]] = make_cell_from(rhs_reg);
                else
                    lily_assign_value(lhs_reg, rhs_reg);

                code += 4;
                break;
            case o_get_upvalue:
                lhs_reg = vm_regs[code[3]];
                rhs_reg = upvalues[code[2]];
                lily_assign_value(lhs_reg, rhs_reg);
                code += 4;
                break;
            case o_optarg_dispatch:
                code += do_o_optarg_dispatch(vm, code);
                break;
            case o_integer_for:
                /* loop_reg is an internal counter, while lhs_reg is an external
                   counter. rhs_reg is the stopping point. */
                loop_reg = vm_regs[code[2]];
                rhs_reg  = vm_regs[code[3]];
                step_reg = vm_regs[code[4]];

                /* Note the use of the loop_reg. This makes it use the internal
                   counter, and thus prevent user assignments from damaging the loop. */
                for_temp = loop_reg->value.integer + step_reg->value.integer;

                /* This idea comes from seeing Lua do something similar. */
                if ((step_reg->value.integer > 0)
                        /* Positive bound check */
                        ? (for_temp <= rhs_reg->value.integer)
                        /* Negative bound check */
                        : (for_temp >= rhs_reg->value.integer)) {

                    /* Haven't reached the end yet, so bump the internal and
                       external values.*/
                    lhs_reg = vm_regs[code[5]];
                    lhs_reg->value.integer = for_temp;
                    loop_reg->value.integer = for_temp;
                    code += 7;
                }
                else
                    code += code[6];

                break;
            case o_push_try:
            {
                if (vm->catch_chain->next == NULL)
                    add_catch_entry(vm);

                lily_vm_catch_entry *catch_entry = vm->catch_chain;
                catch_entry->call_frame = current_frame;
                catch_entry->call_frame_depth = vm->call_depth;
                catch_entry->code_pos = 2 + (code - current_frame->function->code);
                catch_entry->jump_entry = vm->raiser->all_jumps;
                catch_entry->offset_from_main = (int64_t)(vm_regs - regs_from_main);

                vm->catch_chain = vm->catch_chain->next;
                code += 3;
                break;
            }
            case o_pop_try:
                vm->catch_chain = vm->catch_chain->prev;

                code++;
                break;
            case o_raise:
                lhs_reg = vm_regs[code[2]];
                do_o_raise(vm, lhs_reg);
                code += 3;
                break;
            case o_new_instance_basic:
            case o_new_instance_speculative:
            case o_new_instance_tagged:
            {
                do_o_new_instance(vm, code);
                code += 4;
                break;
            }
            case o_match_dispatch:
            {
                /* This opcode is easy because emitter ensures that the match is
                   exhaustive. It also writes down the jumps in order (even if
                   they came out of order). What this does is take the class id
                   of the variant, and drop it so that the first variant is 0,
                   the second is 1, etc. */
                lhs_reg = vm_regs[code[2]];
                /* code[3] is the base enum id + 1. */
                i = lhs_reg->value.instance->instance_id - code[3];

                code += code[5 + i];
                break;
            }
            case o_variant_decompose:
            {
                rhs_reg = vm_regs[code[2]];
                lily_value **decompose_values = rhs_reg->value.instance->values;

                /* Each variant value gets mapped away to a register. The
                   emitter ensures that the decomposition won't go too far. */
                for (i = 0;i < code[3];i++) {
                    lhs_reg = vm_regs[code[4 + i]];
                    lily_assign_value(lhs_reg, decompose_values[i]);
                }

                code += 4 + i;
                break;
            }
            case o_create_closure:
                upvalues = do_o_create_closure(vm, code);
                code += 4;
                break;
            case o_load_class_closure:
                upvalues = do_o_load_class_closure(vm, code);
                code += 5;
                break;
            case o_load_closure:
                upvalues = do_o_load_closure(vm, code);
                code += (code[2] + 4);
                break;
            case o_for_setup:
                /* lhs_reg is the start, rhs_reg is the stop. */
                lhs_reg = vm_regs[code[2]];
                rhs_reg = vm_regs[code[3]];
                step_reg = vm_regs[code[4]];
                loop_reg = vm_regs[code[5]];

                if (step_reg->value.integer == 0)
                    lily_error(vm, SYM_CLASS_VALUEERROR,
                               "for loop step cannot be 0.");

                /* Do a negative step to offset falling into o_for_loop. */
                loop_reg->value.integer =
                        lhs_reg->value.integer - step_reg->value.integer;
                lhs_reg->value.integer = loop_reg->value.integer;
                loop_reg->flags = VAL_IS_INTEGER;

                code += 6;
                break;
            case o_return_from_vm:
                lily_release_jump(vm->raiser);
                return;
        }
    }
}
