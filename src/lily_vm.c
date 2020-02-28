#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include "lily.h"

#include "lily_alloc.h"
#include "lily_vm.h"
#include "lily_parser.h"
#include "lily_value_stack.h"
#include "lily_value_flags.h"
#include "lily_value_raw.h"

#include "lily_int_opcode.h"

extern lily_gc_entry *lily_gc_stopper;
/* This isn't included in a header file because only vm should use this. */
void lily_value_destroy(lily_value *);
/* Same here: Safely escape string values for `KeyError`. */
void lily_mb_escape_add_str(lily_msgbuf *, const char *);

/* Foreign functions set this as their code so that the vm will exit when they
   are to be returned from. */
static uint16_t foreign_code[1] = {o_vm_exit};

/* Operations called from the vm that may raise an error must set the current
   frame's code first. This allows parser and vm to assume that any native
   function's line number exists at code[-1].
   to_add should be the same value added to code at the end of the branch. */
#define SAVE_LINE(to_add) \
current_frame->code = code + to_add

#define INITIAL_REGISTER_COUNT 16

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

static lily_vm_state *new_vm_state(lily_raiser *raiser, int count)
{
    lily_vm_catch_entry *catch_entry = lily_malloc(sizeof(*catch_entry));
    catch_entry->prev = NULL;
    catch_entry->next = NULL;

    int i;
    lily_value **register_base = lily_malloc(count * sizeof(*register_base));

    for (i = 0;i < count;i++) {
        register_base[i] = lily_malloc(sizeof(*register_base[i]));
        register_base[i]->flags = 0;
    }

    lily_value **register_end = register_base + count;

    /* Globals are stored in this frame so they outlive __main__. This allows
       direct calls from outside the interpreter. */
    lily_call_frame *toplevel_frame = lily_malloc(sizeof(*toplevel_frame));

    /* This usually holds __main__, unless something calls into the interpreter
       after execution is done. */
    lily_call_frame *first_frame = lily_malloc(sizeof(*toplevel_frame));

    toplevel_frame->start = register_base;
    toplevel_frame->top = register_base;
    toplevel_frame->register_end = register_end;
    toplevel_frame->code = NULL;
    toplevel_frame->return_target = NULL;
    toplevel_frame->prev = NULL;
    toplevel_frame->next = first_frame;
    first_frame->start = register_base;
    first_frame->top = register_base;
    first_frame->register_end = register_end;
    first_frame->code = NULL;
    first_frame->function = NULL;
    first_frame->return_target = register_base[0];
    first_frame->prev = toplevel_frame;
    first_frame->next = NULL;

    lily_vm_state *vm = lily_malloc(sizeof(*vm));

    vm->call_depth = 0;
    vm->depth_max = 100;
    vm->raiser = raiser;
    vm->catch_chain = NULL;
    vm->call_chain = NULL;
    vm->exception_value = NULL;
    vm->exception_cls = NULL;
    vm->catch_chain = catch_entry;
    /* The parser will enter __main__ when the time comes. */
    vm->call_chain = toplevel_frame;
    vm->vm_buffer = lily_new_msgbuf(64);
    vm->register_root = register_base;

    return vm;
}

lily_vm_state *lily_new_vm_state(lily_raiser *raiser)
{
    lily_vm_state *vm = new_vm_state(raiser, INITIAL_REGISTER_COUNT);
    lily_global_state *gs = lily_malloc(sizeof(*gs));

    gs->regs_from_main = vm->call_chain->start;
    gs->class_table = NULL;
    gs->readonly_table = NULL;
    gs->class_count = 0;
    gs->readonly_count = 0;
    gs->gc_live_entries = NULL;
    gs->gc_spare_entries = NULL;
    gs->gc_live_entry_count = 0;
    gs->gc_pass = 0;
    gs->first_vm = vm;

    vm->gs = gs;

    return vm;
}

void lily_destroy_vm(lily_vm_state *vm)
{
    lily_value **register_root = vm->register_root;
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

    int total = vm->call_chain->register_end - register_root - 1;

    for (i = total;i >= 0;i--) {
        reg = register_root[i];

        lily_deref(reg);

        lily_free(reg);
    }

    lily_free(register_root);

    lily_call_frame *frame_iter = vm->call_chain;
    lily_call_frame *frame_next;

    while (frame_iter->prev)
        frame_iter = frame_iter->prev;

    while (frame_iter) {
        frame_next = frame_iter->next;
        lily_free(frame_iter);
        frame_iter = frame_next;
    }

    lily_free_msgbuf(vm->vm_buffer);
}

static void destroy_gc_entries(lily_vm_state *vm)
{
    lily_global_state *gs = vm->gs;
    lily_gc_entry *gc_iter, *gc_temp;

    if (gs->gc_live_entry_count) {
        /* This function is called after the registers are gone. This walks over
           the remaining gc entries and blasts them just like the gc does. This
           is a two-stage process because the circular values may link back to
           each other. */
        for (gc_iter = gs->gc_live_entries;
             gc_iter;
             gc_iter = gc_iter->next) {
            if (gc_iter->value.generic != NULL) {
                /* This tells value destroy to hollow the value since other
                   circular values may use it. */
                gc_iter->last_pass = -1;
                lily_value_destroy((lily_value *)gc_iter);
            }
        }

        gc_iter = gs->gc_live_entries;

        while (gc_iter) {
            gc_temp = gc_iter->next;

            /* It's either NULL or the remnants of a value. */
            lily_free(gc_iter->value.generic);
            lily_free(gc_iter);

            gc_iter = gc_temp;
        }
    }

    gc_iter = vm->gs->gc_spare_entries;
    while (gc_iter != NULL) {
        gc_temp = gc_iter->next;

        lily_free(gc_iter);

        gc_iter = gc_temp;
    }
}

void lily_rewind_vm(lily_vm_state *vm)
{
    lily_vm_catch_entry *catch_iter = vm->catch_chain;
    lily_call_frame *call_iter = vm->call_chain;

    while (catch_iter->prev)
        catch_iter = catch_iter->prev;

    while (call_iter->prev)
        call_iter = call_iter->prev;

    vm->catch_chain = catch_iter;
    vm->exception_value = NULL;
    vm->exception_cls = NULL;
    vm->call_chain = call_iter;
    vm->call_depth = 0;
}

void lily_free_vm(lily_vm_state *vm)
{
    /* If there are any entries left over, then do a final gc pass that will
       destroy the tagged values. */
    if (vm->gs->gc_live_entry_count)
        invoke_gc(vm);

    lily_destroy_vm(vm);

    destroy_gc_entries(vm);

    lily_free(vm->gs->class_table);
    lily_free(vm->gs);
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
      * lily_value_destroy will collect everything inside a non-circular value,
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
    /* Coroutine vm's can invoke the gc, but the gc is rooted from the vm and
       expands out into others. Make sure that the first one (the right one) is
       the one being used. */
    vm = vm->gs->first_vm;

    /* This is (sort of) a mark-and-sweep garbage collector. This is called when
       a certain number of allocations have been done. Take note that values
       can be destroyed by deref. However, those values will have the gc_entry's
       value set to NULL as an indicator. */
    vm->gs->gc_pass++;

    lily_value **regs_from_main = vm->gs->regs_from_main;
    int pass = vm->gs->gc_pass;
    int i;
    lily_gc_entry *gc_iter;
    int total = vm->call_chain->register_end - vm->gs->regs_from_main;

    /* Stage 1: Go through all registers and use the appropriate gc_marker call
                that will mark every inner value that's visible. */
    for (i = 0;i < total;i++) {
        lily_value *reg = regs_from_main[i];
        if (reg->flags & VAL_HAS_SWEEP_FLAG)
            gc_mark(pass, reg);
    }

    /* Stage 2: Start destroying everything that wasn't marked as visible.
                Don't forget to check ->value for NULL in case the value was
                destroyed through normal ref/deref means. */
    for (gc_iter = vm->gs->gc_live_entries;
         gc_iter;
         gc_iter = gc_iter->next) {
        if (gc_iter->last_pass != pass &&
            gc_iter->value.generic != NULL) {
            /* This tells value destroy to just hollow the value since it may be
               visited multiple times. */
            gc_iter->last_pass = -1;
            lily_value_destroy((lily_value *)gc_iter);
        }
    }

    int current_top = vm->call_chain->top - vm->gs->regs_from_main;

    /* Stage 3: Check registers not currently in use to see if they hold a
                value that's going to be collected. If so, then mark the
                register as nil so that the value will be cleared later. */
    for (i = total;i < current_top;i++) {
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
    lily_gc_entry *new_spare_entries = vm->gs->gc_spare_entries;
    lily_gc_entry *iter_next = NULL;
    gc_iter = vm->gs->gc_live_entries;

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
    if (vm->gs->gc_threshold <= i)
        vm->gs->gc_threshold *= vm->gs->gc_multiplier;

    vm->gs->gc_live_entry_count = i;
    vm->gs->gc_live_entries = new_live_entries;
    vm->gs->gc_spare_entries = new_spare_entries;
}

static void list_marker(int pass, lily_value *v)
{
    if (v->flags & VAL_IS_GC_TAGGED) {
        /* Only instances/enums that pass through here are tagged. */
        lily_gc_entry *e = v->value.container->gc_entry;
        if (e->last_pass == pass)
            return;

        e->last_pass = pass;
    }

    lily_container_val *list_val = v->value.container;
    int i;

    for (i = 0;i < list_val->num_values;i++) {
        lily_value *elem = list_val->values[i];

        if (elem->flags & VAL_HAS_SWEEP_FLAG)
            gc_mark(pass, elem);
    }
}

static void hash_marker(int pass, lily_value *v)
{
    lily_hash_val *hv = v->value.hash;
    int i;

    for (i = 0;i < hv->num_bins;i++) {
        lily_hash_entry *entry = hv->bins[i];
        if (entry)
            gc_mark(pass, entry->record);
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
        if (up && (up->flags & VAL_HAS_SWEEP_FLAG))
            gc_mark(pass, up);
    }
}

static void coroutine_marker(int pass, lily_value *v)
{
    if (v->flags & VAL_IS_GC_TAGGED) {
        lily_gc_entry *e = v->value.function->gc_entry;
        if (e->last_pass == pass)
            return;

        e->last_pass = pass;
    }

    lily_coroutine_val *co_val = v->value.coroutine;
    lily_vm_state *co_vm = co_val->vm;
    lily_value **base = co_vm->register_root;
    int total = co_vm->call_chain->register_end - base - 1;
    int i;

    for (i = total;i >= 0;i--) {
        lily_value *v = base[i];

        if (v->flags & VAL_HAS_SWEEP_FLAG)
            gc_mark(pass, v);
    }

    lily_function_val *base_function = co_val->base_function;

    if (base_function->upvalues) {
        /* If the base Function of the Coroutine has upvalues, they need to be
           walked through. Since the Function is never put into a register, the
           Coroutine's gc tag serves as its tag. */
        lily_value v;
        v.flags = V_FUNCTION_BASE;
        v.value.function = base_function;
        function_marker(pass, &v);
    }

    lily_value *receiver = co_val->receiver;

    if (receiver->flags & VAL_HAS_SWEEP_FLAG)
        gc_mark(pass, receiver);
}

static void gc_mark(int pass, lily_value *v)
{
    if (v->flags & (VAL_IS_GC_TAGGED | VAL_IS_GC_SPECULATIVE)) {
        int base = FLAGS_TO_BASE(v);

        if (base == V_LIST_BASE     || base == V_TUPLE_BASE ||
            base == V_INSTANCE_BASE || base == V_VARIANT_BASE)
            list_marker(pass, v);
        else if (base == V_HASH_BASE)
            hash_marker(pass, v);
        else if (base == V_FUNCTION_BASE)
            function_marker(pass, v);
        else if (base == V_COROUTINE_BASE)
            coroutine_marker(pass, v);
    }
}

/* This will attempt to grab a spare entry and associate it with the value
   given. If there are no spare entries, then a new entry is made. These entries
   are how the gc is able to locate values later.

   If the number of living gc objects is at or past the threshold, then the
   collector will run BEFORE the association. This is intentional, as 'value' is
   not guaranteed to be in a register. */
void lily_value_tag(lily_vm_state *vm, lily_value *v)
{
    lily_global_state *gs = vm->gs;

    if (gs->gc_live_entry_count >= gs->gc_threshold)
        /* Values are rooted in __main__'s vm, but this can be called by a
           Coroutine vm. Make sure invoke always gets the primary vm. */
        invoke_gc(gs->first_vm);

    lily_gc_entry *new_entry;
    if (gs->gc_spare_entries != NULL) {
        new_entry = gs->gc_spare_entries;
        gs->gc_spare_entries = gs->gc_spare_entries->next;
    }
    else
        new_entry = lily_malloc(sizeof(*new_entry));

    new_entry->value.gc_generic = v->value.gc_generic;
    new_entry->last_pass = 0;
    new_entry->flags = v->flags;

    new_entry->next = gs->gc_live_entries;
    gs->gc_live_entries = new_entry;

    /* Attach the gc_entry to the value so the caller doesn't have to. */
    v->value.gc_generic->gc_entry = new_entry;
    gs->gc_live_entry_count++;

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

/** This section handles growing registers and also copying register values over
    for calls. This is also where pushing functions are, which push new values
    onto the stack and increase the top pointer. Those functions are called
    from outside the vm. **/

static void vm_error(lily_vm_state *, uint8_t, const char *);

/* A function has checked and knows it doesn't have enough size left. Ensure
   that there are 'size' more empty spots available. This grows by powers of 2
   so that grows are not frequent.
   This will also fix the locals and top of all frames currently entered. */
static void grow_vm_registers(lily_vm_state *vm, int need)
{
    lily_value **old_start = vm->register_root;
    int size = vm->call_chain->register_end - old_start;
    int i = size;

    need += size;

    do
        size *= 2;
    while (size < need);

    lily_value **new_regs = lily_realloc(old_start, size * sizeof(*new_regs));

    if (vm == vm->gs->first_vm)
        vm->gs->regs_from_main = new_regs;

    /* Now create the registers as a bunch of empty values, to be filled in
       whenever they are needed. */
    for (;i < size;i++) {
        lily_value *v = lily_malloc(sizeof(*v));
        v->flags = 0;

        new_regs[i] = v;
    }

    lily_value **end = new_regs + size;
    lily_call_frame *frame = vm->call_chain;

    while (frame) {
        frame->start = new_regs + (frame->start - old_start);
        frame->top = new_regs + (frame->top - old_start);
        frame->register_end = end;
        frame = frame->prev;
    }

    frame = vm->call_chain->next;
    while (frame) {
        frame->register_end = end;
        frame = frame->next;
    }

    vm->register_root = new_regs;
}

static void vm_setup_before_call(lily_vm_state *vm, uint16_t *code)
{
    lily_call_frame *current_frame = vm->call_chain;
    if (current_frame->next == NULL) {
        if (vm->call_depth > vm->depth_max) {
            SAVE_LINE(code[2] + 5);
            vm_error(vm, LILY_ID_RUNTIMEERROR,
                    "Function call recursion limit reached.");
        }

        add_call_frame(vm);
    }

    int i = code[2];
    current_frame->code = code + i + 5;

    lily_call_frame *next_frame = current_frame->next;
    next_frame->start = current_frame->top;
    next_frame->code = NULL;
    next_frame->return_target = current_frame->start[code[i + 3]];
}

static void clear_extra_registers(lily_call_frame *next_frame, uint16_t *code)
{
    int i = code[2];
    lily_value **target_regs = next_frame->start;

    for (;i < next_frame->function->reg_count;i++) {
        lily_value *reg = target_regs[i];
        lily_deref(reg);

        reg->flags = 0;
    }
}

static void prep_registers(lily_call_frame *frame, uint16_t *code)
{
    lily_call_frame *next_frame = frame->next;
    int i;
    lily_value **input_regs = frame->start;
    lily_value **target_regs = next_frame->start;

    /* A function's args always come first, so copy arguments over while clearing
       old values. */
    for (i = 0;i < code[2];i++) {
        lily_value *get_reg = input_regs[code[3+i]];
        lily_value *set_reg = target_regs[i];

        if (get_reg->flags & VAL_IS_DEREFABLE)
            get_reg->value.generic->refcount++;

        if (set_reg->flags & VAL_IS_DEREFABLE)
            lily_deref(set_reg);

        *set_reg = *get_reg;
    }
}

static lily_string_val *new_sv(char *buffer, int size)
{
    lily_string_val *sv = lily_malloc(sizeof(*sv));
    sv->refcount = 1;
    sv->string = buffer;
    sv->size = size;
    return sv;
}

lily_bytestring_val *lily_new_bytestring_raw(const char *source, int len)
{
    char *buffer = lily_malloc((len + 1) * sizeof(*buffer));
    memcpy(buffer, source, len);
    buffer[len] = '\0';

    return (lily_bytestring_val *)new_sv(buffer, len);
}

lily_string_val *lily_new_string_raw(const char *source)
{
    size_t len = strlen(source);
    char *buffer = lily_malloc((len + 1) * sizeof(*buffer));
    strcpy(buffer, source);

    return new_sv(buffer, len);
}

static lily_container_val *new_container(uint16_t class_id, int num_values)
{
    lily_container_val *cv = lily_malloc(sizeof(*cv));
    cv->values = lily_malloc(num_values * sizeof(*cv->values));
    cv->refcount = 1;
    cv->num_values = num_values;
    cv->extra_space = 0;
    cv->class_id = class_id;
    cv->gc_entry = NULL;

    int i;
    for (i = 0;i < num_values;i++) {
        lily_value *elem = lily_malloc(sizeof(*elem));
        elem->flags = 0;
        cv->values[i] = elem;
    }

    return cv;
}

static void move_byte(lily_value *v, uint8_t z)
{
    if (v->flags & VAL_IS_DEREFABLE)
        lily_deref(v);

    v->value.integer = z;
    v->flags = V_BYTE_FLAG | V_BYTE_BASE;
}

static void move_function_f(uint32_t f, lily_value *v, lily_function_val *z)
{
    if (v->flags & VAL_IS_DEREFABLE)
        lily_deref(v);

    v->value.function = z;
    v->flags = f | V_FUNCTION_BASE | V_FUNCTION_BASE | VAL_IS_DEREFABLE;
}

static void move_hash_f(uint32_t f, lily_value *v, lily_hash_val *z)
{
    if (v->flags & VAL_IS_DEREFABLE)
        lily_deref(v);

    v->value.hash = z;
    v->flags = f | V_HASH_BASE | VAL_IS_DEREFABLE;
}

static void move_instance_f(uint32_t f, lily_value *v, lily_container_val *z)
{
    if (v->flags & VAL_IS_DEREFABLE)
        lily_deref(v);

    v->value.container = z;
    v->flags = f | V_INSTANCE_BASE | VAL_IS_DEREFABLE;
}

static void move_list_f(uint32_t f, lily_value *v, lily_container_val *z)
{
    if (v->flags & VAL_IS_DEREFABLE)
        lily_deref(v);

    v->value.container = z;
    v->flags = f | V_LIST_BASE | VAL_IS_DEREFABLE;
}

static void move_string(lily_value *v, lily_string_val *z)
{
    if (v->flags & VAL_IS_DEREFABLE)
        lily_deref(v);

    v->value.string = z;
    v->flags = VAL_IS_DEREFABLE | V_STRING_FLAG | V_STRING_BASE;
}

static void move_tuple_f(uint32_t f, lily_value *v, lily_container_val *z)
{
    if (v->flags & VAL_IS_DEREFABLE)
        lily_deref(v);

    v->value.container = z;
    v->flags = f | VAL_IS_DEREFABLE | V_TUPLE_BASE;
}

static void move_unit(lily_value *v)
{
    if (v->flags & VAL_IS_DEREFABLE)
        lily_deref(v);

    v->value.integer = 0;
    v->flags = V_UNIT_BASE;
}

static void move_variant_f(uint32_t f, lily_value *v, lily_container_val *z)
{
    if (v->flags & VAL_IS_DEREFABLE)
        lily_deref(v);

    v->value.container = z;
    v->flags = f | VAL_IS_DEREFABLE | V_VARIANT_BASE;
}

#define PUSH_PREAMBLE \
lily_call_frame *frame = s->call_chain; \
if (frame->top == frame->register_end) { \
    grow_vm_registers(s, 1); \
} \
 \
lily_value *target = *frame->top; \
if (target->flags & VAL_IS_DEREFABLE) \
    lily_deref(target); \
 \
frame->top++;

#define SET_TARGET(push_flags, field, push_value) \
target->flags = push_flags; \
target->value.field = push_value

/* This sets gc speculative on the value in case it happens to hold a tagged
   value. The gc will walk values that it shouldn't, but better that than to
   miss a tagged value. It's a small price to pay since marking values as
   speculative doesn't count toward the tag threshold that invokes the gc. */
#define PUSH_CONTAINER(id, container_flags, size) \
PUSH_PREAMBLE \
lily_container_val *c = new_container(id, size); \
SET_TARGET(VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE | container_flags, container, c); \
return c

static void push_coroutine(lily_state *s, lily_coroutine_val *co)
{
    /* The caller will tag the value, so don't set a speculative flag. */
    PUSH_PREAMBLE
    SET_TARGET(V_COROUTINE_BASE | VAL_IS_DEREFABLE, coroutine, co);
}

void lily_push_boolean(lily_state *s, int v)
{
    PUSH_PREAMBLE
    SET_TARGET(V_BOOLEAN_BASE, integer, v);
}

void lily_push_bytestring(lily_state *s, const char *source, int len)
{
    PUSH_PREAMBLE
    char *buffer = lily_malloc((len + 1) * sizeof(*buffer));
    memcpy(buffer, source, len);
    buffer[len] = '\0';

    lily_string_val *sv = new_sv(buffer, len);

    SET_TARGET(V_BYTESTRING_FLAG | V_BYTESTRING_BASE | VAL_IS_DEREFABLE, string, sv);
}

void lily_push_byte(lily_state *s, uint8_t v)
{
    PUSH_PREAMBLE
    SET_TARGET(V_BYTE_FLAG | V_BYTE_BASE, integer, v);
}

void lily_push_double(lily_state *s, double v)
{
    PUSH_PREAMBLE
    SET_TARGET(V_DOUBLE_FLAG | V_DOUBLE_BASE, doubleval, v);
}

void lily_push_empty_variant(lily_state *s, uint16_t id)
{
    PUSH_PREAMBLE
    SET_TARGET(V_EMPTY_VARIANT_BASE, integer, id);
}

void lily_push_file(lily_state *s, FILE *inner_file, const char *mode)
{
    PUSH_PREAMBLE
    lily_file_val *filev = lily_malloc(sizeof(*filev));

    int plus = strchr(mode, '+') != NULL;

    filev->refcount = 1;
    filev->inner_file = inner_file;
    filev->read_ok = (*mode == 'r' || plus);
    filev->write_ok = (*mode == 'w' || plus);
    filev->is_builtin = 0;

    SET_TARGET(V_FILE_BASE | VAL_IS_DEREFABLE, file, filev);
}

lily_foreign_val *lily_push_foreign(lily_state *s, uint16_t id,
        lily_destroy_func func, size_t size)
{
    PUSH_PREAMBLE
    lily_foreign_val *fv = lily_malloc(size * sizeof(*fv));
    fv->refcount = 1;
    fv->class_id = id;
    fv->destroy_func = func;

    SET_TARGET(VAL_IS_DEREFABLE | V_FOREIGN_BASE, foreign, fv);
    return fv;
}

lily_hash_val *lily_push_hash(lily_state *s, int size)
{
    PUSH_PREAMBLE
    lily_hash_val *h = lily_new_hash_raw(size);
    SET_TARGET(V_HASH_BASE | VAL_IS_DEREFABLE | VAL_IS_GC_SPECULATIVE, hash, h);
    return h;
}

lily_container_val *lily_push_instance(lily_state *s, uint16_t id,
        uint32_t size)
{
    PUSH_CONTAINER(id, V_INSTANCE_BASE, size);
}

void lily_push_integer(lily_state *s, int64_t v)
{
    PUSH_PREAMBLE
    SET_TARGET(V_INTEGER_FLAG | V_INTEGER_BASE, integer, v);
}

lily_container_val *lily_push_list(lily_state *s, uint32_t size)
{
    PUSH_CONTAINER(LILY_ID_LIST, V_LIST_BASE, size);
}

lily_container_val *lily_push_super(lily_state *s, uint16_t id,
        uint32_t initial)
{
    lily_value *v = s->call_chain->return_target;

    if (FLAGS_TO_BASE(v) == V_INSTANCE_BASE) {
        lily_container_val *pending_instance = v->value.container;
        if (pending_instance->instance_ctor_need != 0) {
            pending_instance->instance_ctor_need = 0;
            lily_push_value(s, v);
            return pending_instance;
        }
    }

    /* Create a new instance of the foreign-based native class. This native
       class is considered completely constructed. In the case of foreign-based
       native classes with inheritance (Exception and friends), the caller must
       run the relevant inits.
       This is not an issue with true native classes that inherit foreign-based
       native classes, because they'll be trapped by the above. */
    lily_container_val *cv = lily_push_instance(s, id, initial);
    cv->instance_ctor_need = 0;
    return cv;
}

void lily_push_string(lily_state *s, const char *source)
{
    PUSH_PREAMBLE
    size_t len = strlen(source);
    char *buffer = lily_malloc((len + 1) * sizeof(*buffer));
    strcpy(buffer, source);

    lily_string_val *sv = new_sv(buffer, len);

    SET_TARGET(V_STRING_FLAG | V_STRING_BASE | VAL_IS_DEREFABLE, string, sv);
}

void lily_push_string_sized(lily_state *s, const char *source, int len)
{
    PUSH_PREAMBLE
    char *buffer = lily_malloc((len + 1) * sizeof(*buffer));
    memcpy(buffer, source, len);
    buffer[len] = '\0';

    lily_string_val *sv = new_sv(buffer, len);

    SET_TARGET(V_STRING_FLAG | V_STRING_BASE | VAL_IS_DEREFABLE, string, sv);
}

lily_container_val *lily_push_tuple(lily_state *s, uint32_t size)
{
    PUSH_CONTAINER(LILY_ID_TUPLE, V_TUPLE_BASE, size);
}

void lily_push_unit(lily_state *s)
{
    PUSH_PREAMBLE
    SET_TARGET(V_UNIT_BASE, integer, 0);
}

void lily_push_value(lily_state *s, lily_value *v)
{
    PUSH_PREAMBLE
    if (v->flags & VAL_IS_DEREFABLE)
        v->value.generic->refcount++;

    target->flags = v->flags;
    target->value = v->value;
}

lily_container_val *lily_push_variant(lily_state *s, uint16_t id, uint32_t size)
{
    PUSH_CONTAINER(id, V_VARIANT_BASE, size);
}

#define RETURN_PREAMBLE \
lily_value *target = s->call_chain->return_target; \
if (target->flags & VAL_IS_DEREFABLE) \
    lily_deref(target);

void lily_return_boolean(lily_state *s, int v)
{
    RETURN_PREAMBLE
    SET_TARGET(V_BOOLEAN_BASE, integer, v);
}

void lily_return_byte(lily_state *s, uint8_t v)
{
    RETURN_PREAMBLE
    SET_TARGET(V_BYTE_FLAG | V_BYTE_BASE, integer, v);
}

void lily_return_double(lily_state *s, double v)
{
    RETURN_PREAMBLE
    SET_TARGET(V_DOUBLE_FLAG | V_DOUBLE_BASE, doubleval, v);
}

void lily_return_integer(lily_state *s, int64_t v)
{
    RETURN_PREAMBLE
    SET_TARGET(V_INTEGER_FLAG | V_INTEGER_BASE, integer, v);
}

void lily_return_none(lily_state *s)
{
    RETURN_PREAMBLE
    SET_TARGET(V_EMPTY_VARIANT_BASE, integer, LILY_ID_NONE);
}

void lily_return_super(lily_state *s)
{
    lily_value *target = s->call_chain->return_target;
    lily_value *top = *(s->call_chain->top - 1);

    if (FLAGS_TO_BASE(target) == V_INSTANCE_BASE &&
        target->value.container == top->value.container) {
        return;
    }

    if (target->flags & VAL_IS_DEREFABLE)
        lily_deref(target);

    *target = *top;
    top->flags = 0;
}

void lily_return_top(lily_state *s)
{
    lily_value *target = s->call_chain->return_target;
    if (target->flags & VAL_IS_DEREFABLE)
        lily_deref(target);

    lily_value *top = *(s->call_chain->top - 1);
    *target = *top;

    top->flags = 0;
}

void lily_return_unit(lily_state *s)
{
    RETURN_PREAMBLE
    SET_TARGET(V_UNIT_BASE, container, NULL);
}

void lily_return_value(lily_state *s, lily_value *v)
{
    lily_value *target = s->call_chain->return_target;
    lily_value_assign(target, v);
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
    lily_call_frame *new_frame = lily_malloc(sizeof(*new_frame));

    new_frame->prev = vm->call_chain;
    new_frame->next = NULL;
    new_frame->return_target = NULL;
    /* The toplevel and __main__ frames are allocated directly, so there's
       always a next and a register end set. */
    new_frame->register_end = vm->call_chain->register_end;

    vm->call_chain->next = new_frame;
    vm->call_chain = new_frame;
}

static void add_catch_entry(lily_vm_state *vm)
{
    lily_vm_catch_entry *new_entry = lily_malloc(sizeof(*new_entry));

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

static void dispatch_exception(lily_vm_state *vm);

static void load_exception(lily_vm_state *vm, uint8_t id)
{
    lily_class *c = vm->gs->class_table[id];
    if (c == NULL) {
        /* What this does is to kick parser's exception bootstrapping machinery
           into gear in order to load the exception that's needed. This is
           unfortunate, but the vm doesn't have a sane and easy way to properly
           build classes here. */
        c = lily_dynaload_exception(vm->gs->parser,
                names[id - LILY_ID_EXCEPTION]);

        /* The above will store at least one new function. It's extremely rare,
           but possible, for that to trigger a grow of symtab's literals. If
           realloc moves the underlying data, then vm->gs->readonly_table will
           be invalid. Make sure that doesn't happen. */
        vm->gs->readonly_table = vm->gs->parser->symtab->literals->data;
        vm->gs->class_table[id] = c;
    }

    vm->exception_cls = c;
}

/* This raises an error in the vm that won't have a proper value backing it. The
   id should be the id of some exception class. This may run a faux dynaload of
   the error, so that printing has a class name to go by. */
static void vm_error(lily_vm_state *vm, uint8_t id, const char *message)
{
    load_exception(vm, id);

    lily_msgbuf *msgbuf = lily_mb_flush(vm->vm_buffer);
    lily_mb_add(msgbuf, message);

    dispatch_exception(vm);
}

#define LILY_ERROR(err, id) \
void lily_##err##Error(lily_vm_state *vm, const char *fmt, ...) \
{ \
    lily_msgbuf *msgbuf = lily_mb_flush(vm->vm_buffer); \
 \
    va_list var_args; \
    va_start(var_args, fmt); \
    lily_mb_add_fmt_va(msgbuf, fmt, var_args); \
    va_end(var_args); \
 \
    load_exception(vm, id); \
    dispatch_exception(vm); \
}

LILY_ERROR(DivisionByZero, LILY_ID_DBZERROR)
LILY_ERROR(Index,          LILY_ID_INDEXERROR)
LILY_ERROR(IO,             LILY_ID_IOERROR)
LILY_ERROR(Key,            LILY_ID_KEYERROR)
LILY_ERROR(Runtime,        LILY_ID_RUNTIMEERROR)
LILY_ERROR(Value,          LILY_ID_VALUEERROR)

/* Raise KeyError with 'key' as the value of the message. */
static void key_error(lily_vm_state *vm, lily_value *key, uint16_t line_num)
{
    lily_msgbuf *msgbuf = lily_mb_flush(vm->vm_buffer);

    if (key->flags & V_STRING_FLAG)
        lily_mb_escape_add_str(msgbuf, key->value.string->string);
    else
        lily_mb_add_fmt(msgbuf, "%ld", key->value.integer);

    load_exception(vm, LILY_ID_KEYERROR);
    dispatch_exception(vm);
}

/* Raise IndexError, noting that 'bad_index' is, well, bad. */
static void boundary_error(lily_vm_state *vm, int64_t bad_index,
        uint16_t line_num)
{
    lily_msgbuf *msgbuf = lily_mb_flush(vm->vm_buffer);
    lily_mb_add_fmt(msgbuf, "Subscript index %ld is out of range.",
            bad_index);

    load_exception(vm, LILY_ID_INDEXERROR);
    dispatch_exception(vm);
}

/***
 *      ____        _ _ _   _
 *     | __ ) _   _(_) | |_(_)_ __  ___
 *     |  _ \| | | | | | __| | '_ \/ __|
 *     | |_) | |_| | | | |_| | | | \__ \
 *     |____/ \__,_|_|_|\__|_|_| |_|___/
 *
 */

static lily_container_val *build_traceback_raw(lily_vm_state *);

void lily_builtin__calltrace(lily_vm_state *vm)
{
    /* Omit calltrace from the call stack since it's useless info. */
    vm->call_depth--;
    vm->call_chain = vm->call_chain->prev;

    lily_container_val *trace = build_traceback_raw(vm);

    vm->call_depth++;
    vm->call_chain = vm->call_chain->next;

    move_list_f(0, vm->call_chain->return_target, trace);
}

static void do_print(lily_vm_state *vm, FILE *target, lily_value *source)
{
    if (source->flags & V_STRING_FLAG)
        fputs(source->value.string->string, target);
    else {
        lily_msgbuf *msgbuf = lily_mb_flush(vm->vm_buffer);
        lily_mb_add_value(msgbuf, vm, source);
        fputs(lily_mb_raw(msgbuf), target);
    }

    fputc('\n', target);
    lily_return_unit(vm);
}

void lily_builtin__print(lily_vm_state *vm)
{
    do_print(vm, stdout, lily_arg_value(vm, 0));
}

/* Initially, print is implemented through lily_builtin__print. However, when
   stdout is dynaloaded, that doesn't work. When stdout is found, print needs to
   use the register holding Lily's stdout, not the plain C stdout. */
void lily_stdout_print(lily_vm_state *vm)
{
    /* This uses a couple of tricks not available to proper api functions.
       First, it sets the cid table to point to the id of the var holding
       stdin.
       The other trick is to use regs_from_main to grab the globals. */
    uint16_t spot = *vm->call_chain->function->cid_table;
    lily_file_val *stdout_val = vm->gs->regs_from_main[spot]->value.file;
    if (stdout_val->inner_file == NULL)
        vm_error(vm, LILY_ID_VALUEERROR, "IO operation on closed file.");

    do_print(vm, stdout_val->inner_file, lily_arg_value(vm, 0));
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
static void do_o_property_set(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->call_chain->start;
    lily_value *rhs_reg;
    int index;
    lily_container_val *ival;

    index = code[1];
    ival = vm_regs[code[2]]->value.container;
    rhs_reg = vm_regs[code[3]];

    lily_value_assign(ival->values[index], rhs_reg);
}

static void do_o_property_get(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->call_chain->start;
    lily_value *result_reg;
    int index;
    lily_container_val *ival;

    index = code[1];
    ival = vm_regs[code[2]]->value.container;
    result_reg = vm_regs[code[3]];

    lily_value_assign(result_reg, ival->values[index]);
}

#define RELATIVE_INDEX(limit) \
    if (index_int < 0) { \
        int64_t new_index = limit + index_int; \
        if (new_index < 0) \
            boundary_error(vm, index_int, code[4]); \
 \
        index_int = new_index; \
    } \
    else if (index_int >= limit) \
        boundary_error(vm, index_int, code[4]);

/* This handles subscript assignment. The index is a register, and needs to be
   validated. */
static void do_o_subscript_set(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->call_chain->start;
    lily_value *lhs_reg, *index_reg, *rhs_reg;
    uint16_t base;

    lhs_reg = vm_regs[code[1]];
    index_reg = vm_regs[code[2]];
    rhs_reg = vm_regs[code[3]];
    base = FLAGS_TO_BASE(lhs_reg);

    if (base != V_HASH_BASE) {
        int64_t index_int = index_reg->value.integer;

        if (base == V_BYTESTRING_BASE) {
            lily_string_val *bytev = lhs_reg->value.string;
            RELATIVE_INDEX(bytev->size)
            bytev->string[index_int] = (char)rhs_reg->value.integer;
        }
        else {
            /* List and Tuple have the same internal representation. */
            lily_container_val *list_val = lhs_reg->value.container;
            RELATIVE_INDEX(list_val->num_values)
            lily_value_assign(list_val->values[index_int], rhs_reg);
        }
    }
    else
        lily_hash_set(vm, lhs_reg->value.hash, index_reg, rhs_reg);
}

/* This handles subscript access. The index is a register, and needs to be
   validated. */
static void do_o_subscript_get(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->call_chain->start;
    lily_value *lhs_reg, *index_reg, *result_reg;
    uint16_t base;

    lhs_reg = vm_regs[code[1]];
    index_reg = vm_regs[code[2]];
    result_reg = vm_regs[code[3]];
    base = FLAGS_TO_BASE(lhs_reg);

    if (base != V_HASH_BASE) {
        int64_t index_int = index_reg->value.integer;

        if (base == V_BYTESTRING_BASE) {
            lily_string_val *bytev = lhs_reg->value.string;
            RELATIVE_INDEX(bytev->size)
            move_byte(result_reg, (uint8_t) bytev->string[index_int]);
        }
        else {
            /* List and Tuple have the same internal representation. */
            lily_container_val *list_val = lhs_reg->value.container;
            RELATIVE_INDEX(list_val->num_values)
            lily_value_assign(result_reg, list_val->values[index_int]);
        }
    }
    else {
        lily_value *elem = lily_hash_get(vm, lhs_reg->value.hash, index_reg);

        /* Give up if the key doesn't exist. */
        if (elem == NULL)
            key_error(vm, index_reg, code[4]);

        lily_value_assign(result_reg, elem);
    }
}

#undef RELATIVE_INDEX

static void do_o_build_hash(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->call_chain->start;
    int i, num_values;
    lily_value *result, *key_reg, *value_reg;

    num_values = code[2];
    result = vm_regs[code[3 + num_values]];

    lily_hash_val *hash_val = lily_new_hash_raw(num_values / 2);

    for (i = 0;
         i < num_values;
         i += 2) {
        key_reg = vm_regs[code[3 + i]];
        value_reg = vm_regs[code[3 + i + 1]];

        lily_hash_set(vm, hash_val, key_reg, value_reg);
    }

    move_hash_f(VAL_IS_GC_SPECULATIVE, result, hash_val);
}

/* Lists and tuples are effectively the same thing internally, since the list
   value holds proper values. This is used primarily to do as the name suggests.
   However, variant types are also tuples (but with a different name). */
static void do_o_build_list_tuple(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->call_chain->start;
    int num_elems = code[1];
    lily_value *result = vm_regs[code[2+num_elems]];
    lily_container_val *lv;

    if (code[0] == o_build_list) {
        lv = new_container(LILY_ID_LIST, num_elems);
    }
    else {
        lv = (lily_container_val *)new_container(LILY_ID_TUPLE, num_elems);
    }

    lily_value **elems = lv->values;

    int i;
    for (i = 0;i < num_elems;i++) {
        lily_value *rhs_reg = vm_regs[code[2+i]];
        lily_value_assign(elems[i], rhs_reg);
    }

    if (code[0] == o_build_list)
        move_list_f(VAL_IS_GC_SPECULATIVE, result, lv);
    else
        move_tuple_f(VAL_IS_GC_SPECULATIVE, result, (lily_container_val *)lv);
}

static void do_o_build_variant(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->call_chain->start;
    int variant_id = code[1];
    int count = code[2];
    lily_value *result = vm_regs[code[code[2] + 3]];

    lily_container_val *ival = new_container(variant_id, count);
    lily_value **slots = ival->values;

    int i;
    for (i = 0;i < count;i++) {
        lily_value *rhs_reg = vm_regs[code[3+i]];
        lily_value_assign(slots[i], rhs_reg);
    }

    move_variant_f(VAL_IS_GC_SPECULATIVE, result, ival);
}

/* This raises a user-defined exception. The emitter has verified that the thing
   to be raised is raiseable (extends Exception). */
static void do_o_exception_raise(lily_vm_state *vm, lily_value *exception_val)
{
    /* The Exception class has values[0] as the message, values[1] as the
       container for traceback. */

    lily_container_val *ival = exception_val->value.container;
    char *message = ival->values[0]->value.string->string;
    lily_class *raise_cls = vm->gs->class_table[ival->class_id];

    /* There's no need for a ref/deref here, because the gc cannot trigger
       foreign stack unwind and/or exception capture. */
    vm->exception_value = exception_val;
    vm->exception_cls = raise_cls;

    lily_msgbuf *msgbuf = lily_mb_flush(vm->vm_buffer);
    lily_mb_add(msgbuf, message);

    dispatch_exception(vm);
}

/* This creates a new instance of a class. This checks if the current call is
   part of a constructor chain. If so, it will attempt to use the value
   currently being built instead of making a new one.
   There are three opcodes that come in through here. This will use the incoming
   opcode as a way of deducing what to do with the newly-made instance. */
static void do_o_new_instance(lily_vm_state *vm, uint16_t *code)
{
    int cls_id = code[1];
    lily_value **vm_regs = vm->call_chain->start;
    lily_value *result = vm_regs[code[2]];

    /* Is the caller a superclass building an instance already? */
    lily_value *pending_value = vm->call_chain->return_target;
    if (FLAGS_TO_BASE(pending_value) == V_INSTANCE_BASE) {
        lily_container_val *cv = pending_value->value.container;

        if (cv->instance_ctor_need) {
            cv->instance_ctor_need--;
            lily_value_assign(result, pending_value);
            return;
        }
    }

    lily_class *instance_class = vm->gs->class_table[cls_id];
    int total_entries = instance_class->prop_count;
    lily_container_val *iv = new_container(cls_id, total_entries);

    iv->instance_ctor_need = instance_class->inherit_depth;

    uint32_t flags =
        (instance_class->flags & CLS_GC_FLAGS) << VAL_FROM_CLS_GC_SHIFT;

    if (flags == VAL_IS_GC_SPECULATIVE)
        move_instance_f(VAL_IS_GC_SPECULATIVE, result, iv);
    else {
        move_instance_f(0, result, iv);
        if (flags == VAL_IS_GC_TAGGED)
            lily_value_tag(vm, result);
    }
}

static void do_o_interpolation(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->call_chain->start;
    int count = code[1];
    lily_msgbuf *vm_buffer = lily_mb_flush(vm->vm_buffer);

    int i;
    for (i = 0;i < count;i++) {
        lily_value *v = vm_regs[code[2 + i]];
        lily_mb_add_value(vm_buffer, vm, v);
    }

    lily_value *result_reg = vm_regs[code[2 + i]];

    lily_string_val *sv = lily_new_string_raw(lily_mb_raw(vm_buffer));
    move_string(result_reg, sv);
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
    lily_value *result = lily_malloc(sizeof(*result));
    *result = *value;
    result->cell_refcount = 1;
    if (value->flags & VAL_IS_DEREFABLE)
        value->value.generic->refcount++;

    return result;
}

/* This clones the data inside of 'to_copy'. */
static lily_function_val *new_function_copy(lily_function_val *to_copy)
{
    lily_function_val *f = lily_malloc(sizeof(*f));

    *f = *to_copy;
    f->refcount = 1;

    return f;
}

/* This opcode is the bottom level of closure creation. It is responsible for
   creating the original closure. */
static lily_value **do_o_closure_new(lily_vm_state *vm, uint16_t *code)
{
    int count = code[1];
    lily_value *result = vm->call_chain->start[code[2]];

    lily_function_val *last_call = vm->call_chain->function;

    lily_function_val *closure_func = new_function_copy(last_call);

    lily_value **upvalues = lily_malloc(sizeof(*upvalues) * count);

    /* Cells are initially NULL so that o_closure_set knows to copy a new value
       into a cell. */
    int i;
    for (i = 0;i < count;i++)
        upvalues[i] = NULL;

    closure_func->num_upvalues = count;
    closure_func->upvalues = upvalues;

    /* Put the closure into a register so that the gc has an easy time of
       finding it. This also helps to ensure it goes away in a more predictable
       manner, in case there aren't many gc objects. */
    move_function_f(0, result, closure_func);
    lily_value_tag(vm, result);

    /* Swap out the currently-entered function. This will make it so that all
       closures have upvalues set on the frame to draw from. */
    vm->call_chain->function = closure_func;

    return upvalues;
}

/* This copies cells from 'source' to 'target'. Cells that exist are given a
   cell_refcount bump. */
static void copy_upvalues(lily_function_val *target, lily_function_val *source)
{
    lily_value **source_upvalues = source->upvalues;
    int count = source->num_upvalues;

    lily_value **new_upvalues = lily_malloc(sizeof(*new_upvalues) * count);
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
static void do_o_closure_function(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->call_chain->start;
    lily_function_val *input_closure = vm->call_chain->function;

    lily_value *target = vm->gs->readonly_table[code[1]];
    lily_function_val *target_func = target->value.function;

    lily_value *result_reg = vm_regs[code[2]];
    lily_function_val *new_closure = new_function_copy(target_func);

    copy_upvalues(new_closure, input_closure);

    uint16_t *locals = new_closure->proto->locals;
    if (locals) {
        lily_value **upvalues = new_closure->upvalues;
        int i, end = locals[0];
        for (i = 1;i < end;i++) {
            int pos = locals[i];
            lily_value *up = upvalues[pos];
            if (up) {
                up->cell_refcount--;
                upvalues[pos] = NULL;
            }
        }
    }

    move_function_f(VAL_IS_GC_SPECULATIVE, result_reg, new_closure);
    lily_value_tag(vm, result_reg);
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
static lily_container_val *build_traceback_raw(lily_vm_state *vm)
{
    lily_call_frame *frame_iter = vm->call_chain;
    int depth = vm->call_depth;
    int i;

    lily_msgbuf *msgbuf = lily_msgbuf_get(vm);
    lily_container_val *lv = new_container(LILY_ID_LIST, depth);

    /* The call chain goes from the most recent to least. Work around that by
       allocating elements in reverse order. It's safe to do this because
       nothing in this loop can trigger the gc. */
    for (i = depth;
         i >= 1;
         i--, frame_iter = frame_iter->prev) {
        lily_function_val *func_val = frame_iter->function;
        lily_proto *proto = func_val->proto;
        const char *path = proto->module_path;
        char line[16] = "";
        if (func_val->code)
            sprintf(line, "%d:", frame_iter->code[-1]);

        const char *str = lily_mb_sprintf(msgbuf, "%s:%s in %s", path,
                line, proto->name);

        lily_string_val *sv = lily_new_string_raw(str);
        move_string(lv->values[i - 1], sv);
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
    const char *raw_message = lily_mb_raw(vm->vm_buffer);
    lily_container_val *ival = new_container(raised_cls->id, 2);

    lily_string_val *sv = lily_new_string_raw(raw_message);
    move_string(ival->values[0], sv);

    move_list_f(0, ival->values[1], build_traceback_raw(vm));

    move_instance_f(VAL_IS_GC_SPECULATIVE, result, ival);
}

/* This is called when 'raise' raises an error. The traceback property is
   assigned to freshly-made traceback. The other fields of the value are left
   intact, however. */
static void fixup_exception_val(lily_vm_state *vm, lily_value *result)
{
    lily_value_assign(result, vm->exception_value);
    lily_container_val *raw_trace = build_traceback_raw(vm);
    lily_container_val *iv = result->value.container;

    move_list_f(VAL_IS_GC_SPECULATIVE, lily_con_get(iv, 1), raw_trace);
}

/* This is called when the vm has raised an exception. This changes control to
   a jump that handles the error (some `except` clause), or parser. */
static void dispatch_exception(lily_vm_state *vm)
{
    lily_raiser *raiser = vm->raiser;
    lily_class *raised_cls = vm->exception_cls;
    lily_vm_catch_entry *catch_iter = vm->catch_chain->prev;
    int match = 0;
    int jump_location;
    uint16_t *code;

    vm->exception_cls = raised_cls;

    while (catch_iter != NULL) {
        /* Foreign functions register callbacks so they can fix values when
           there is an error. Put the state where it was when the callback was
           registered and go back. The callback shouldn't execute code. */
        if (catch_iter->catch_kind == catch_callback) {
            vm->call_chain = catch_iter->call_frame;
            vm->call_depth = catch_iter->call_frame_depth;
            catch_iter->callback_func(vm);
            catch_iter = catch_iter->prev;
            continue;
        }

        lily_call_frame *call_frame = catch_iter->call_frame;
        code = call_frame->function->code;
        /* A try block is done when the next jump is at 0 (because 0 would
           always be going back, which is illogical otherwise). */
        jump_location = catch_iter->code_pos + code[catch_iter->code_pos] - 1;

        while (1) {
            lily_class *catch_class =
                    vm->gs->class_table[code[jump_location + 1]];

            if (lily_class_greater_eq(catch_class, raised_cls)) {
                /* ...So that execution resumes from within the except block. */
                jump_location += 4;
                match = 1;
                break;
            }
            else {
                int move_by = code[jump_location + 2];
                if (move_by == 0)
                    break;

                jump_location += move_by;
            }
        }

        if (match)
            break;

        catch_iter = catch_iter->prev;
    }

    lily_jump_link *jump_stop;

    if (match) {
        code += jump_location;
        if (*code == o_exception_store) {
            lily_value *catch_reg = catch_iter->call_frame->start[code[1]];

            /* There is a var that the exception needs to be dropped into. If
               this exception was triggered by raise, then use that (after
               dumping traceback into it). If not, create a new instance to
               hold the info. */
            if (vm->exception_value)
                fixup_exception_val(vm, catch_reg);
            else
                make_proper_exception_val(vm, raised_cls, catch_reg);

            code += 2;
        }

        /* Make sure any exception value that was held is gone. No ref/deref is
           necessary, because the value was saved somewhere in a register. */
        vm->exception_value = NULL;
        vm->exception_cls = NULL;
        vm->call_chain = catch_iter->call_frame;
        vm->call_depth = catch_iter->call_frame_depth;
        vm->call_chain->code = code;
        /* Each try block can only successfully handle one exception, so use
           ->prev to prevent using the same block again. */
        vm->catch_chain = catch_iter;

        jump_stop = catch_iter->jump_entry->prev;

        while (raiser->all_jumps->prev != jump_stop)
            raiser->all_jumps = raiser->all_jumps->prev;
    }
    else {
        while (raiser->all_jumps->prev != NULL)
            raiser->all_jumps = raiser->all_jumps->prev;

        lily_raise_class(vm->raiser, vm->exception_cls,
                         lily_mb_raw(vm->vm_buffer));
        /* Since nothing in vm can capture the error, go to the first jump. The
           first jump is always parser's jump. */
        jump_stop = NULL;
        /* Throw the message into the raiser's msgbuf so the parser's traceback
           has a consistent place for getting the message. */
        const char *message = lily_mb_raw(vm->vm_buffer);

        lily_mb_flush(vm->raiser->msgbuf);
        lily_mb_add(vm->raiser->msgbuf, message);
    }


    longjmp(raiser->all_jumps->jump, 1);
}

/***
 *       ____                       _   _
 *      / ___| ___  _ __ ___  _   _| |_(_)_ __   ___ ___
 *     | |    / _ \| '__/ _ \| | | | __| | '_ \ / _ | __|
 *     | |___| (_) | | | (_) | |_| | |_| | | | |  __|__ \
 *      \____|\___/|_|  \___/ \__,_|\__|_|_| |_|\___|___/
 *
 */

/** A coroutine is a special kind of function that can yield a value and suspend
    itself for later. There are different kinds of coroutines depending on the
    language, with their capabilities differing.

    Lily's implementation of coroutines is to create them as a value holding a
    vm. A coroutine vm is different than the one created by 'lily_new_state' in
    that it shares the global state of the calling vm.

    Because of this design, coroutine vms have their own stack and their own
    exception state. The vms share a common global state in part to prevent a
    parse from making coroutine tables stale.

    Static typing makes implementing coroutines more difficult. A coroutine
    cannot yield any kind of a value, and may want an initial set of extra
    arguments.

    The first problem is solved by requiring coroutines take a coroutine as
    their first argument. Unfortunately, this solution means that coroutines
    will almost always need a garbage collection cycle to go away.

    The second is somewhat solved by having a coroutine constructor that takes a
    single extra argument. It is believed that few coroutines have a valid
    reason for 2+ one-time arguments. Those that want such a feature can pass a
    Tuple for the callee to unpack, or use a closure to store arguments. **/

static lily_coroutine_val *new_coroutine(lily_vm_state *base_vm,
        lily_function_val *base_function)
{
    lily_coroutine_val *result = lily_malloc(sizeof(*result));
    lily_value *receiver = lily_malloc(sizeof(*receiver));

    /* This is ignored when resuming through .resume, and overwritten when
       resuming through .resume_with. */
    receiver->flags = V_UNIT_BASE;

    result->refcount = 1;
    result->class_id = LILY_ID_COROUTINE;
    result->status = co_waiting;
    result->vm = base_vm;
    result->base_function = base_function;
    result->receiver = receiver;

    return result;
}

/* This marks the Coroutine's base Function as having ownership of the arguments
   that were just passed. It's effectively lily_call, except that there's no
   registers to zero (they were just made), and no growth check (vm creation
   already make sure of that). */
static void coroutine_call_prep(lily_vm_state *vm, int count)
{
    lily_call_frame *source_frame = vm->call_chain;
    lily_call_frame *target_frame = vm->call_chain->next;

    /* The last 'count' arguments go from the old frame to the new one. */
    target_frame->top = source_frame->top;
    source_frame->top -= count;
    target_frame->start = source_frame->top;

    vm->call_depth++;

    target_frame->top += target_frame->function->reg_count - count;
    vm->call_chain = target_frame;
}

static lily_state *coroutine_build(lily_state *s)
{
    lily_function_val *to_copy = lily_arg_function(s, 0);

    if (to_copy->foreign_func != NULL)
        lily_RuntimeError(s, "Only native functions can be coroutines.");

    lily_function_val *base_func = new_function_copy(to_copy);

    if (to_copy->upvalues)
        copy_upvalues(base_func, to_copy);
    else
        base_func->upvalues = NULL;

    lily_vm_state *base_vm = new_vm_state(lily_new_raiser(),
            INITIAL_REGISTER_COUNT + to_copy->reg_count);
    lily_call_frame *toplevel_frame = base_vm->call_chain;

    base_vm->gs = s->gs;
    base_vm->depth_max = s->depth_max;
    /* Bail out of the vm loop if the Coroutine's base Function completes. */
    toplevel_frame->code = foreign_code;
    /* Don't crash when returning to the toplevel frame. */
    toplevel_frame->function = base_func;

    /* Make the Coroutine and hand it arguments. The first is the Coroutine
       itself (for control), then whatever arguments this builder was given. */

    lily_coroutine_val *co_val = new_coroutine(base_vm, base_func);

    push_coroutine(s, co_val);
    /* Tag before pushing so that both sides have the gc tag flag. */
    lily_value_tag(s, lily_stack_get_top(s));

    /* This has the side-effect of pushing a Unit register at the very bottom as
       register zero. This is later used by yield since the base function cannot
       touch it. */
    lily_call_prepare(base_vm, base_func);
    lily_push_value(base_vm, lily_stack_get_top(s));

    return base_vm;
}

void lily_builtin_Coroutine_build(lily_state *s)
{
    lily_state *base_vm = coroutine_build(s);

    coroutine_call_prep(base_vm, 1);
    lily_return_top(s);
}

void lily_builtin_Coroutine_build_with_value(lily_state *s)
{
    lily_state *base_vm = coroutine_build(s);

    lily_push_value(base_vm, lily_arg_value(s, 1));

    coroutine_call_prep(base_vm, 2);
    lily_return_top(s);
}

void lily_builtin_Coroutine_receive(lily_state *s)
{
    lily_coroutine_val *co_val = lily_arg_coroutine(s, 0);

    if (co_val->vm != s)
        lily_RuntimeError(s,
                "Attempt to receive a value from another coroutine.");

    lily_push_value(s, co_val->receiver);
    lily_return_top(s);
}

static void coroutine_resume(lily_vm_state *origin, lily_coroutine_val *co_val,
        lily_value *to_send)
{
    /* Don't resume Coroutines that are already running, done, or broken. */
    if (co_val->status != co_waiting) {
        lily_push_none(origin);
        return;
    }

    lily_vm_state *target = co_val->vm;
    lily_coroutine_status new_status = co_running;

    if (to_send)
        lily_value_assign(co_val->receiver, to_send);

    co_val->status = co_running;

    /* If the vm absolutely has to bail out, it uses the very first jump as the
       target. Make it point to here. */
    lily_jump_link *jump_base = target->raiser->all_jumps;

    lily_value *result = NULL;

    if (setjmp(jump_base->jump) == 0) {
        /* Invoke the vm loop. It'll pick up from where it left off at. If the
           vm raises or yields, one of the other cases will be reached. */
        lily_vm_execute(target);

        new_status = co_done;
    }
    else if (target->exception_cls == NULL) {
        /* The Coroutine yielded a value that's at the top of its stack. */
        result = lily_stack_get_top(target);

        new_status = co_waiting;

        /* Since the Coroutine jumped back instead of exiting through the main
           loop, the call state needs to be fixed. */
        target->call_chain = target->call_chain->prev;
        target->call_depth--;
    }
    else
        /* An exception was raised, so there's nothing to return. */
        new_status = co_failed;

    co_val->status = new_status;

    if (result) {
        lily_container_val *con = lily_push_some(origin);
        lily_push_value(origin, result);
        lily_con_set_from_stack(origin, con, 0);
    }
    else
        lily_push_none(origin);
}

void lily_builtin_Coroutine_resume(lily_state *s)
{
    coroutine_resume(s, lily_arg_coroutine(s, 0), NULL);
    lily_return_top(s);
}

void lily_builtin_Coroutine_resume_with(lily_state *s)
{
    coroutine_resume(s, lily_arg_coroutine(s, 0), lily_arg_value(s, 1));
    lily_return_top(s);
}

void lily_builtin_Coroutine_yield(lily_state *s)
{
    lily_coroutine_val *co_target = lily_arg_coroutine(s, 0);
    lily_value *to_yield = lily_arg_value(s, 1);

    lily_vm_state *co_vm = co_target->vm;

    if (co_vm != s)
        lily_RuntimeError(s, "Cannot yield from another coroutine.");

    lily_raiser *co_raiser = co_vm->raiser;

    /* A vm always has at least two jumps currently active:
     * 1: Parser, or the coroutine base.
     * 2: vm main loop.
       If there are any more jumps, the vm is in a foreign call. A Coroutine in
       a foreign call cannot be restored, because restoration happens by calling
       the vm main loop again. */
    if (co_raiser->all_jumps->prev->prev != NULL)
        lily_RuntimeError(s, "Cannot yield while in a foreign call.");

    /* The yield will not come back, so the return must come first. */
    lily_return_unit(s);

    /* Push the value to be yielded so that the caller has an obvious place to
       find it (top of the stack). The value must be popped before the
       Coroutine is resumed. */
    lily_push_value(co_vm, to_yield);

    /* Since yield is jumping back into the base jump, the main loop is never
       properly exited. The main loop's jump needs to be popped so that it can
       be properly restored when the main loop is entered again. */
    lily_release_jump(co_raiser);

    longjmp(co_raiser->all_jumps->jump, 1);
}

/***
 *      _____              _                  _    ____ ___
 *     |  ___|__  _ __ ___(_) __ _ _ __      / \  |  _ \_ _|
 *     | |_ / _ \| '__/ _ \ |/ _` | '_ \    / _ \ | |_) | |
 *     |  _| (_) | | |  __/ | (_| | | | |  / ___ \|  __/| |
 *     |_|  \___/|_|  \___|_|\__, |_| |_| /_/   \_\_|  |___|
 *                           |___/
 */

lily_msgbuf *lily_msgbuf_get(lily_vm_state *vm)
{
    return lily_mb_flush(vm->vm_buffer);
}

/** Foreign functions that are looking to interact with the interpreter can use
    the functions within here. Do be careful with foreign calls, however. **/

void lily_call_prepare(lily_vm_state *vm, lily_function_val *func)
{
    lily_call_frame *caller_frame = vm->call_chain;
    caller_frame->code = foreign_code;

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
    target_frame->return_target = *caller_frame->top;

    lily_push_unit(vm);
}

lily_value *lily_call_result(lily_vm_state *vm)
{
    return vm->call_chain->next->return_target;
}

void lily_call(lily_vm_state *vm, int count)
{
    lily_call_frame *source_frame = vm->call_chain;
    lily_call_frame *target_frame = vm->call_chain->next;
    lily_function_val *target_fn = target_frame->function;

    /* The last 'count' arguments go from the old frame to the new one. */
    target_frame->top = source_frame->top;
    source_frame->top -= count;
    target_frame->start = source_frame->top;

    vm->call_depth++;

    if (target_fn->code == NULL) {
        vm->call_chain = target_frame;
        target_fn->foreign_func(vm);

        vm->call_chain = target_frame->prev;
        vm->call_depth--;
    }
    else {
        /* lily_vm_execute determines the starting code position by tapping the
           frame's code. That allows coroutines to resume where they left off
           at. Regular functions like this one start at the top every time, and
           this ensures that. */
        target_frame->code = target_fn->code;

        int diff = target_frame->function->reg_count - count;

        if (target_frame->top + diff > target_frame->register_end) {
            vm->call_chain = target_frame;
            grow_vm_registers(vm, diff);
        }

        lily_value **start = target_frame->top;
        lily_value **end = target_frame->top + diff;
        while (start != end) {
            lily_value *v = *start;
            lily_deref(v);
            v->flags = 0;
            start++;
        }

        target_frame->top += diff;
        vm->call_chain = target_frame;

        lily_vm_execute(vm);

        /* Native execute drops the frame and lowers the depth. Nothing more to
           do for it. */
    }
}

void lily_error_callback_push(lily_state *s, lily_error_callback_func func)
{
    if (s->catch_chain->next == NULL)
        add_catch_entry(s);

    lily_vm_catch_entry *catch_entry = s->catch_chain;
    catch_entry->call_frame = s->call_chain;
    catch_entry->call_frame_depth = s->call_depth;
    catch_entry->callback_func = func;
    catch_entry->catch_kind = catch_callback;

    s->catch_chain = s->catch_chain->next;
}

void lily_error_callback_pop(lily_state *s)
{
    s->catch_chain = s->catch_chain->prev;
}

/***
 *      ____
 *     |  _ \ _ __ ___ _ __
 *     | |_) | '__/ _ \ '_ \
 *     |  __/| | |  __/ |_) |
 *     |_|   |_|  \___| .__/
 *                    |_|
 */

/** These functions are used by symtab to help prepare the vm's class table. The
    class table is used in different areas of the vm to provide a quick mapping
    from class id to actual class. Usage examples include class initialization
    and printing classes. **/

void lily_vm_ensure_class_table(lily_vm_state *vm, int size)
{
    int old_count = vm->gs->class_count;

    if (size >= vm->gs->class_count) {
        if (vm->gs->class_count == 0)
            vm->gs->class_count = 1;

        while (size >= vm->gs->class_count)
            vm->gs->class_count *= 2;

        vm->gs->class_table = lily_realloc(vm->gs->class_table,
                sizeof(*vm->gs->class_table) * vm->gs->class_count);
    }

    /* For the first pass, make sure the spots for Exception and its built-in
       children are zero'ed out. This allows vm_error to safely check if an
       exception class has been loaded by testing the class field for being NULL
       (and relies on holes being set aside for these exceptions). */
    if (old_count == 0) {
        int i;
        for (i = LILY_ID_EXCEPTION;i < LILY_ID_UNIT;i++)
            vm->gs->class_table[i] = NULL;
    }
}

void lily_vm_add_class_unchecked(lily_vm_state *vm, lily_class *cls)
{
    vm->gs->class_table[cls->id] = cls;
}

/***
 *      _____                     _
 *     | ____|_  _____  ___ _   _| |_ ___
 *     |  _| \ \/ / _ \/ __| | | | __/ _ \
 *     | |___ >  <  __/ (__| |_| | ||  __/
 *     |_____/_/\_\___|\___|\__,_|\__\___|
 *
 */

#define INTEGER_OP(OP) \
lhs_reg = vm_regs[code[1]]; \
rhs_reg = vm_regs[code[2]]; \
vm_regs[code[3]]->value.integer = \
lhs_reg->value.integer OP rhs_reg->value.integer; \
vm_regs[code[3]]->flags = V_INTEGER_FLAG | V_INTEGER_BASE; \
code += 5;

#define DOUBLE_OP(OP) \
lhs_reg = vm_regs[code[1]]; \
rhs_reg = vm_regs[code[2]]; \
vm_regs[code[3]]->value.doubleval = \
lhs_reg->value.doubleval OP rhs_reg->value.doubleval; \
vm_regs[code[3]]->flags = V_DOUBLE_FLAG | V_DOUBLE_BASE; \
code += 5;

/* EQUALITY_COMPARE_OP is used for == and !=, instead of a normal COMPARE_OP.
   The difference is that this will allow op on any type, so long as the lhs
   and rhs agree on the full type. This allows comparing functions, hashes
   lists, and more.

   Arguments are:
   * op:       The operation to perform relative to the values given. This will
               be substituted like: lhs->value OP rhs->value
               Do it for string too, but against 0. */
#define EQUALITY_COMPARE_OP(OP) \
lhs_reg = vm_regs[code[1]]; \
rhs_reg = vm_regs[code[2]]; \
if (lhs_reg->flags & V_DOUBLE_FLAG) { \
    vm_regs[code[3]]->value.integer = \
    (lhs_reg->value.doubleval OP rhs_reg->value.doubleval); \
} \
else if (lhs_reg->flags & V_INTEGER_FLAG) { \
    vm_regs[code[3]]->value.integer =  \
    (lhs_reg->value.integer OP rhs_reg->value.integer); \
} \
else if (lhs_reg->flags & V_STRING_FLAG) { \
    vm_regs[code[3]]->value.integer = \
    strcmp(lhs_reg->value.string->string, \
           rhs_reg->value.string->string) OP 0; \
} \
else { \
    SAVE_LINE(+5); \
    vm_regs[code[3]]->value.integer = \
    lily_value_compare(vm, lhs_reg, rhs_reg) OP 1; \
} \
vm_regs[code[3]]->flags = V_BOOLEAN_BASE; \
code += 5;

#define COMPARE_OP(OP) \
lhs_reg = vm_regs[code[1]]; \
rhs_reg = vm_regs[code[2]]; \
if (lhs_reg->flags & V_DOUBLE_FLAG) { \
    vm_regs[code[3]]->value.integer = \
    (lhs_reg->value.doubleval OP rhs_reg->value.doubleval); \
} \
else if (lhs_reg->flags & (V_INTEGER_FLAG | V_BYTE_FLAG)) { \
    vm_regs[code[3]]->value.integer = \
    (lhs_reg->value.integer OP rhs_reg->value.integer); \
} \
else if (lhs_reg->flags & V_STRING_FLAG) { \
    vm_regs[code[3]]->value.integer = \
    strcmp(lhs_reg->value.string->string, \
           rhs_reg->value.string->string) OP 0; \
} \
vm_regs[code[3]]->flags = V_BOOLEAN_BASE; \
code += 5;

void lily_vm_execute(lily_vm_state *vm)
{
    uint16_t *code;
    lily_value **vm_regs;
    int i;
    register int64_t for_temp;
    register lily_value *lhs_reg, *rhs_reg, *loop_reg, *step_reg;
    lily_function_val *fval;
    lily_value **upvalues = NULL;

    lily_call_frame *current_frame = vm->call_chain;
    lily_call_frame *next_frame = NULL;

    code = current_frame->code;

    lily_jump_link *link = lily_jump_setup(vm->raiser);
    if (setjmp(link->jump) != 0) {
        /* This section happens when an exception is caught. */
        current_frame = vm->call_chain;
        code = current_frame->code;
    }

    upvalues = current_frame->function->upvalues;
    vm_regs = vm->call_chain->start;

    while (1) {
        switch(code[0]) {
            case o_assign_noref:
                rhs_reg = vm_regs[code[1]];
                lhs_reg = vm_regs[code[2]];
                lhs_reg->flags = rhs_reg->flags;
                lhs_reg->value = rhs_reg->value;
                code += 4;
                break;
            case o_load_readonly:
                rhs_reg = vm->gs->readonly_table[code[1]];
                lhs_reg = vm_regs[code[2]];

                lily_deref(lhs_reg);

                lhs_reg->value = rhs_reg->value;
                lhs_reg->flags = rhs_reg->flags;
                code += 4;
                break;
            case o_load_empty_variant:
                lhs_reg = vm_regs[code[2]];

                lily_deref(lhs_reg);

                lhs_reg->value.integer = code[1];
                lhs_reg->flags = V_EMPTY_VARIANT_BASE;
                code += 4;
                break;
            case o_load_integer:
                lhs_reg = vm_regs[code[2]];
                lhs_reg->value.integer = (int16_t)code[1];
                lhs_reg->flags = V_INTEGER_FLAG | V_INTEGER_BASE;
                code += 4;
                break;
            case o_load_boolean:
                lhs_reg = vm_regs[code[2]];
                lhs_reg->value.integer = code[1];
                lhs_reg->flags = V_BOOLEAN_BASE;
                code += 4;
                break;
            case o_load_byte:
                lhs_reg = vm_regs[code[2]];
                lhs_reg->value.integer = (uint8_t)code[1];
                lhs_reg->flags = V_BYTE_FLAG | V_BYTE_BASE;
                code += 4;
                break;
            case o_int_add:
                INTEGER_OP(+)
                break;
            case o_int_minus:
                INTEGER_OP(-)
                break;
            case o_number_add:
                DOUBLE_OP(+)
                break;
            case o_number_minus:
                DOUBLE_OP(-)
                break;
            case o_compare_eq:
                EQUALITY_COMPARE_OP(==)
                break;
            case o_compare_greater:
                COMPARE_OP(>)
                break;
            case o_compare_greater_eq:
                COMPARE_OP(>=)
                break;
            case o_compare_not_eq:
                EQUALITY_COMPARE_OP(!=)
                break;
            case o_jump:
                code += (int16_t)code[1];
                break;
            case o_int_multiply:
                INTEGER_OP(*)
                break;
            case o_number_multiply:
                DOUBLE_OP(*)
                break;
            case o_int_divide:
                /* Before doing INTEGER_OP, check for a division by zero. This
                   will involve some redundant checking of the rhs, but better
                   than dumping INTEGER_OP's contents here or rewriting
                   INTEGER_OP for the special case of division. */
                rhs_reg = vm_regs[code[2]];
                if (rhs_reg->value.integer == 0) {
                    SAVE_LINE(+5);
                    vm_error(vm, LILY_ID_DBZERROR,
                            "Attempt to divide by zero.");
                }
                INTEGER_OP(/)
                break;
            case o_int_modulo:
                /* x % 0 will do the same thing as x / 0... */
                rhs_reg = vm_regs[code[2]];
                if (rhs_reg->value.integer == 0) {
                    SAVE_LINE(+5);
                    vm_error(vm, LILY_ID_DBZERROR,
                            "Attempt to divide by zero.");
                }

                INTEGER_OP(%)
                break;
            case o_int_left_shift:
                INTEGER_OP(<<)
                break;
            case o_int_right_shift:
                INTEGER_OP(>>)
                break;
            case o_int_bitwise_and:
                INTEGER_OP(&)
                break;
            case o_int_bitwise_or:
                INTEGER_OP(|)
                break;
            case o_int_bitwise_xor:
                INTEGER_OP(^)
                break;
            case o_number_divide:
                rhs_reg = vm_regs[code[2]];
                if (rhs_reg->value.doubleval == 0) {
                    SAVE_LINE(+5);
                    vm_error(vm, LILY_ID_DBZERROR,
                            "Attempt to divide by zero.");
                }

                DOUBLE_OP(/)
                break;
            case o_jump_if:
                lhs_reg = vm_regs[code[2]];
                {
                    int base = FLAGS_TO_BASE(lhs_reg);
                    int result;

                    if (base == V_INTEGER_BASE || base == V_BOOLEAN_BASE)
                        result = (lhs_reg->value.integer == 0);
                    else if (base == V_STRING_BASE)
                        result = (lhs_reg->value.string->size == 0);
                    else if (base == V_LIST_BASE)
                        result = (lhs_reg->value.container->num_values == 0);
                    else
                        result = 1;

                    if (result != code[1])
                        code += (int16_t)code[3];
                    else
                        code += 4;
                }
                break;
            case o_call_foreign:
                fval = vm->gs->readonly_table[code[1]]->value.function;

                foreign_func_body: ;

                vm_setup_before_call(vm, code);

                i = code[2];

                next_frame = current_frame->next;
                next_frame->function = fval;
                next_frame->top = next_frame->start + i;

                if (next_frame->top >= next_frame->register_end) {
                    vm->call_chain = next_frame;
                    grow_vm_registers(vm, i + 1);
                }

                prep_registers(current_frame, code);

                vm_regs = next_frame->start;
                vm->call_chain = next_frame;
                vm->call_depth++;

                fval->foreign_func(vm);

                vm->call_depth--;
                vm->call_chain = current_frame;
                vm_regs = current_frame->start;
                code = current_frame->code;

                break;
            case o_call_native: {
                fval = vm->gs->readonly_table[code[1]]->value.function;

                native_func_body: ;

                vm_setup_before_call(vm, code);

                next_frame = current_frame->next;
                next_frame->function = fval;
                next_frame->top = next_frame->start + fval->reg_count;

                if (next_frame->top >= next_frame->register_end) {
                    vm->call_chain = next_frame;
                    grow_vm_registers(vm, fval->reg_count);
                }

                prep_registers(current_frame, code);
                /* A native Function call is almost certainly going to have more
                   registers than arguments. They need to be blasted. This is
                   not part of prep_registers, because foreign functions do not
                   have this same requirement. */
                clear_extra_registers(next_frame, code);

                current_frame = current_frame->next;
                vm->call_chain = current_frame;
                vm->call_depth++;

                vm_regs = current_frame->start;
                code = fval->code;
                upvalues = fval->upvalues;

                break;
            }
            case o_call_register:
                fval = vm_regs[code[1]]->value.function;

                if (fval->code != NULL)
                    goto native_func_body;
                else
                    goto foreign_func_body;

                break;
            case o_interpolation:
                do_o_interpolation(vm, code);
                code += code[1] + 4;
                break;
            case o_unary_not:
                lhs_reg = vm_regs[code[1]];

                rhs_reg = vm_regs[code[2]];
                rhs_reg->flags = lhs_reg->flags;
                rhs_reg->value.integer = !(lhs_reg->value.integer);
                code += 4;
                break;
            case o_unary_minus:
                lhs_reg = vm_regs[code[1]];

                rhs_reg = vm_regs[code[2]];

                if (lhs_reg->flags & V_INTEGER_FLAG) {
                    rhs_reg->value.integer = -(lhs_reg->value.integer);
                    rhs_reg->flags = V_INTEGER_FLAG | V_INTEGER_BASE;
                }
                else {
                    rhs_reg->value.doubleval = -(lhs_reg->value.doubleval);
                    rhs_reg->flags = V_DOUBLE_FLAG | V_DOUBLE_BASE;
                }

                code += 4;
                break;
            case o_unary_bitwise_not:
                lhs_reg = vm_regs[code[1]];

                rhs_reg = vm_regs[code[2]];
                rhs_reg->flags = lhs_reg->flags;
                rhs_reg->value.integer = ~(lhs_reg->value.integer);
                code += 4;
                break;
            case o_return_unit:
                move_unit(current_frame->return_target);
                goto return_common;

            case o_return_value:
                lhs_reg = current_frame->return_target;
                rhs_reg = vm_regs[code[1]];
                lily_value_assign(lhs_reg, rhs_reg);

                return_common: ;

                current_frame = current_frame->prev;
                vm->call_chain = current_frame;
                vm->call_depth--;

                vm_regs = current_frame->start;
                upvalues = current_frame->function->upvalues;
                code = current_frame->code;
                break;
            case o_global_get:
                rhs_reg = vm->gs->regs_from_main[code[1]];
                lhs_reg = vm_regs[code[2]];

                lily_value_assign(lhs_reg, rhs_reg);
                code += 4;
                break;
            case o_global_set:
                rhs_reg = vm_regs[code[1]];
                lhs_reg = vm->gs->regs_from_main[code[2]];

                lily_value_assign(lhs_reg, rhs_reg);
                code += 4;
                break;
            case o_assign:
                rhs_reg = vm_regs[code[1]];
                lhs_reg = vm_regs[code[2]];

                lily_value_assign(lhs_reg, rhs_reg);
                code += 4;
                break;
            case o_subscript_get:
                /* Might raise IndexError or KeyError. */
                SAVE_LINE(+5);
                do_o_subscript_get(vm, code);
                code += 5;
                break;
            case o_property_get:
                do_o_property_get(vm, code);
                code += 5;
                break;
            case o_subscript_set:
                /* Might raise IndexError or KeyError. */
                SAVE_LINE(+5);
                do_o_subscript_set(vm, code);
                code += 5;
                break;
            case o_property_set:
                do_o_property_set(vm, code);
                code += 5;
                break;
            case o_build_hash:
                do_o_build_hash(vm, code);
                code += code[2] + 5;
                break;
            case o_build_list:
            case o_build_tuple:
                do_o_build_list_tuple(vm, code);
                code += code[1] + 4;
                break;
            case o_build_variant:
                do_o_build_variant(vm, code);
                code += code[2] + 5;
                break;
            case o_closure_function:
                do_o_closure_function(vm, code);
                code += 4;
                break;
            case o_closure_set:
                lhs_reg = upvalues[code[1]];
                rhs_reg = vm_regs[code[2]];
                if (lhs_reg == NULL)
                    upvalues[code[1]] = make_cell_from(rhs_reg);
                else
                    lily_value_assign(lhs_reg, rhs_reg);

                code += 4;
                break;
            case o_closure_get:
                lhs_reg = vm_regs[code[2]];
                rhs_reg = upvalues[code[1]];
                lily_value_assign(lhs_reg, rhs_reg);
                code += 4;
                break;
            case o_for_integer:
                /* loop_reg is an internal counter, while lhs_reg is an external
                   counter. rhs_reg is the stopping point. */
                loop_reg = vm_regs[code[1]];
                rhs_reg  = vm_regs[code[2]];
                step_reg = vm_regs[code[3]];

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
                    lhs_reg = vm_regs[code[4]];
                    lhs_reg->value.integer = for_temp;
                    loop_reg->value.integer = for_temp;
                    code += 7;
                }
                else
                    code += code[5];

                break;
            case o_catch_push:
            {
                if (vm->catch_chain->next == NULL)
                    add_catch_entry(vm);

                lily_vm_catch_entry *catch_entry = vm->catch_chain;
                catch_entry->call_frame = current_frame;
                catch_entry->call_frame_depth = vm->call_depth;
                catch_entry->code_pos = 1 + (code - current_frame->function->code);
                catch_entry->jump_entry = vm->raiser->all_jumps;
                catch_entry->catch_kind = catch_native;

                vm->catch_chain = vm->catch_chain->next;
                code += 3;
                break;
            }
            case o_catch_pop:
                vm->catch_chain = vm->catch_chain->prev;

                code++;
                break;
            case o_exception_raise:
                SAVE_LINE(+3);
                lhs_reg = vm_regs[code[1]];
                do_o_exception_raise(vm, lhs_reg);
                code += 3;
                break;
            case o_instance_new:
            {
                do_o_new_instance(vm, code);
                code += 4;
                break;
            }
            case o_jump_if_not_class:
                lhs_reg = vm_regs[code[2]];
                i = FLAGS_TO_BASE(lhs_reg);

                /* This opcode is used for match branches. The source is always
                   a class instance or a variant (which might be empty). */
                if (i == V_VARIANT_BASE || i == V_INSTANCE_BASE)
                    i = lhs_reg->value.container->class_id;
                else
                    i = (uint16_t)lhs_reg->value.integer;

                if (i == code[1])
                    code += 4;
                else
                    code += code[3];

                break;
            case o_jump_if_set:
                lhs_reg = vm_regs[code[1]];

                if (lhs_reg->flags == 0)
                    code += 3;
                else
                    code += code[2];

                break;
            case o_closure_new:
                do_o_closure_new(vm, code);
                upvalues = current_frame->function->upvalues;
                code += 4;
                break;
            case o_for_setup:
                /* lhs_reg is the start, rhs_reg is the stop. */
                lhs_reg = vm_regs[code[1]];
                rhs_reg = vm_regs[code[2]];
                step_reg = vm_regs[code[3]];
                loop_reg = vm_regs[code[4]];

                if (step_reg->value.integer == 0)
                    vm_error(vm, LILY_ID_VALUEERROR,
                               "for loop step cannot be 0.");

                /* Do a negative step to offset falling into o_for_loop. */
                loop_reg->value.integer =
                        lhs_reg->value.integer - step_reg->value.integer;
                lhs_reg->value.integer = loop_reg->value.integer;
                loop_reg->flags = V_INTEGER_FLAG | V_INTEGER_BASE;

                code += 6;
                break;
            case o_vm_exit:
                lily_release_jump(vm->raiser);
                return;
            default:
                return;
        }
    }
}
