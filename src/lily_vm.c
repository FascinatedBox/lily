
#include <stddef.h>
#include <string.h>

#include "lily_alloc.h"
#include "lily_vm.h"
#include "lily_parser.h"
#include "lily_value_stack.h"
#include "lily_value_flags.h"
#include "lily_value_raw.h"
#include "lily_move.h"

#include "lily_int_opcode.h"
#include "lily_api_value.h"

extern lily_gc_entry *lily_gc_stopper;
/* This isn't included in a header file because only vm should use this. */
void lily_value_destroy(lily_value *);
/* Same here: Safely escape string values for `KeyError`. */
void lily_mb_escape_add_str(lily_msgbuf *, const char *);

#define INTEGER_OP(OP) \
lhs_reg = vm_regs[code[2]]; \
rhs_reg = vm_regs[code[3]]; \
vm_regs[code[4]]->value.integer = \
lhs_reg->value.integer OP rhs_reg->value.integer; \
vm_regs[code[4]]->flags = LILY_INTEGER_ID; \
code += 5;

#define DOUBLE_OP(OP) \
lhs_reg = vm_regs[code[2]]; \
rhs_reg = vm_regs[code[3]]; \
vm_regs[code[4]]->value.doubleval = \
lhs_reg->value.doubleval OP rhs_reg->value.doubleval; \
vm_regs[code[4]]->flags = LILY_DOUBLE_ID; \
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
lhs_reg = vm_regs[code[2]]; \
rhs_reg = vm_regs[code[3]]; \
if (lhs_reg->class_id == LILY_DOUBLE_ID) { \
    vm_regs[code[4]]->value.integer = \
    (lhs_reg->value.doubleval OP rhs_reg->value.doubleval); \
} \
else if (lhs_reg->class_id == LILY_INTEGER_ID) { \
    vm_regs[code[4]]->value.integer =  \
    (lhs_reg->value.integer OP rhs_reg->value.integer); \
} \
else if (lhs_reg->class_id == LILY_STRING_ID) { \
    vm_regs[code[4]]->value.integer = \
    strcmp(lhs_reg->value.string->string, \
           rhs_reg->value.string->string) OP 0; \
} \
else { \
    vm->pending_line = code[1]; \
    vm_regs[code[4]]->value.integer = \
    lily_value_compare(vm, lhs_reg, rhs_reg) OP 1; \
} \
vm_regs[code[4]]->flags = LILY_BOOLEAN_ID; \
code += 5;

#define COMPARE_OP(OP) \
lhs_reg = vm_regs[code[2]]; \
rhs_reg = vm_regs[code[3]]; \
if (lhs_reg->class_id == LILY_DOUBLE_ID) { \
    vm_regs[code[4]]->value.integer = \
    (lhs_reg->value.doubleval OP rhs_reg->value.doubleval); \
} \
else if (lhs_reg->class_id == LILY_INTEGER_ID) { \
    vm_regs[code[4]]->value.integer = \
    (lhs_reg->value.integer OP rhs_reg->value.integer); \
} \
else if (lhs_reg->class_id == LILY_STRING_ID) { \
    vm_regs[code[4]]->value.integer = \
    strcmp(lhs_reg->value.string->string, \
           rhs_reg->value.string->string) OP 0; \
} \
vm_regs[code[4]]->flags = LILY_BOOLEAN_ID; \
code += 5;

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

lily_vm_state *lily_new_vm_state(lily_raiser *raiser)
{
    lily_vm_catch_entry *catch_entry = lily_malloc(sizeof(*catch_entry));
    catch_entry->prev = NULL;
    catch_entry->next = NULL;

    int i, count = 16;
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
    toplevel_frame->line_num = 0;
    toplevel_frame->prev = NULL;
    toplevel_frame->next = first_frame;
    first_frame->start = register_base;
    first_frame->top = register_base;
    first_frame->register_end = register_end;
    first_frame->code = NULL;
    first_frame->function = NULL;
    first_frame->return_target = register_base[0];
    first_frame->line_num = 0;
    first_frame->prev = toplevel_frame;
    first_frame->next = NULL;

    lily_vm_state *vm = lily_malloc(sizeof(*vm));

    vm->call_depth = 0;
    vm->raiser = raiser;
    vm->regs_from_main = NULL;
    vm->gc_live_entries = NULL;
    vm->gc_spare_entries = NULL;
    vm->gc_live_entry_count = 0;
    vm->gc_pass = 0;
    vm->catch_chain = NULL;
    vm->readonly_table = NULL;
    vm->readonly_count = 0;
    vm->call_chain = NULL;
    vm->class_count = 0;
    vm->class_table = NULL;
    vm->exception_value = NULL;
    vm->pending_line = 0;
    vm->catch_chain = catch_entry;
    /* lily_vm_prep will handle entering __main__ when the time comes. */
    vm->call_chain = toplevel_frame;
    vm->regs_from_main = register_base;

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

    /* If there are any entries left over, then do a final gc pass that will
       destroy the tagged values. */
    if (vm->gc_live_entry_count)
        invoke_gc(vm);

    int total = vm->call_chain->register_end - regs_from_main - 1;

    for (i = total;i >= 0;i--) {
        reg = regs_from_main[i];

        lily_deref(reg);

        lily_free(reg);
    }

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
    /* This is (sort of) a mark-and-sweep garbage collector. This is called when
       a certain number of allocations have been done. Take note that values
       can be destroyed by deref. However, those values will have the gc_entry's
       value set to NULL as an indicator. */
    vm->gc_pass++;

    lily_value **regs_from_main = vm->regs_from_main;
    int pass = vm->gc_pass;
    int i;
    lily_gc_entry *gc_iter;
    int total = vm->call_chain->register_end - vm->regs_from_main;

    /* Stage 1: Go through all registers and use the appropriate gc_marker call
                that will mark every inner value that's visible. */
    for (i = 0;i < total;i++) {
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
            lily_value_destroy((lily_value *)gc_iter);
        }
    }

    int current_top = vm->call_chain->top - vm->regs_from_main;

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
        lily_gc_entry *e = v->value.container->gc_entry;
        if (e->last_pass == pass)
            return;

        e->last_pass = pass;
    }

    lily_container_val *c = lily_as_container(v);
    lily_value *inner_value = lily_con_get(c, 0);

    if (inner_value->flags & VAL_IS_GC_SWEEPABLE)
        gc_mark(pass, inner_value);
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

        if (elem->flags & VAL_IS_GC_SWEEPABLE)
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
        if (up && (up->flags & VAL_IS_GC_SWEEPABLE))
            gc_mark(pass, up);
    }
}

static void gc_mark(int pass, lily_value *v)
{
    if (v->flags & (VAL_IS_GC_TAGGED | VAL_IS_GC_SPECULATIVE)) {
        int class_id = v->class_id;
        if (class_id == LILY_LIST_ID ||
            class_id == LILY_TUPLE_ID ||
            v->flags & (VAL_IS_ENUM | VAL_IS_INSTANCE))
            list_marker(pass, v);
        else if (class_id == LILY_HASH_ID)
            hash_marker(pass, v);
        else if (class_id == LILY_DYNAMIC_ID)
            dynamic_marker(pass, v);
        else if (class_id == LILY_FUNCTION_ID)
            function_marker(pass, v);
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
    if (vm->gc_live_entry_count >= vm->gc_threshold)
        invoke_gc(vm);

    lily_gc_entry *new_entry;
    if (vm->gc_spare_entries != NULL) {
        new_entry = vm->gc_spare_entries;
        vm->gc_spare_entries = vm->gc_spare_entries->next;
    }
    else
        new_entry = lily_malloc(sizeof(*new_entry));

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

/* A function has checked and knows it doesn't have enough size left. Ensure
   that there are 'size' more empty spots available. This grows by powers of 2
   so that grows are not frequent.
   This will also fix the locals and top of all frames currently entered. */
static void grow_vm_registers(lily_vm_state *vm, int need)
{
    int size = vm->call_chain->register_end - vm->regs_from_main;
    int i = size;

    need += size;

    do
        size *= 2;
    while (size < need);

    lily_value **old_start = vm->regs_from_main;
    lily_value **new_regs = lily_realloc(vm->regs_from_main, size *
            sizeof(*new_regs));

    vm->regs_from_main = new_regs;

    /* Now create the registers as a bunch of empty values, to be filled in
       whenever they are needed. */
    for (;i < size;i++) {
        lily_value *v = lily_malloc(sizeof(*v));
        v->flags = 0;

        new_regs[i] = v;
    }

    lily_value **end = vm->regs_from_main + size;
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
}

static void prep_registers(lily_call_frame *frame, uint16_t *code)
{
    lily_call_frame *next_frame = frame->next;
    int i;
    lily_value **input_regs = frame->start;
    lily_value **target_regs = next_frame->start;

    /* A function's args always come first, so copy arguments over while clearing
       old values. */
    for (i = 0;i < code[3];i++) {
        lily_value *get_reg = input_regs[code[4+i]];
        lily_value *set_reg = target_regs[i];

        if (get_reg->flags & VAL_IS_DEREFABLE)
            get_reg->value.generic->refcount++;

        if (set_reg->flags & VAL_IS_DEREFABLE)
            lily_deref(set_reg);

        *set_reg = *get_reg;
    }

    for (;i < next_frame->function->reg_count;i++) {
        lily_value *reg = target_regs[i];
        lily_deref(reg);

        reg->flags = 0;
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

#define PUSH_CONTAINER(id, container_flags, size) \
PUSH_PREAMBLE \
lily_container_val *c = new_container(id, size); \
SET_TARGET(id | VAL_IS_DEREFABLE | VAL_IS_CONTAINER | container_flags, container, c); \
return c

void lily_push_boolean(lily_state *s, int v)
{
    PUSH_PREAMBLE
    SET_TARGET(LILY_BOOLEAN_ID, integer, v);
}

void lily_push_bytestring(lily_state *s, const char *source, int len)
{
    PUSH_PREAMBLE
    char *buffer = lily_malloc((len + 1) * sizeof(*buffer));
    memcpy(buffer, source, len);
    buffer[len] = '\0';

    lily_string_val *sv = new_sv(buffer, len);

    SET_TARGET(LILY_BYTESTRING_ID | VAL_IS_DEREFABLE, string, sv);
}

void lily_push_byte(lily_state *s, uint8_t v)
{
    PUSH_PREAMBLE
    SET_TARGET(LILY_BYTE_ID, integer, v);
}

void lily_push_double(lily_state *s, double v)
{
    PUSH_PREAMBLE
    SET_TARGET(LILY_DOUBLE_ID, doubleval, v);
}

lily_container_val *lily_push_dynamic(lily_state *s)
{
    PUSH_CONTAINER(LILY_DYNAMIC_ID, VAL_IS_INSTANCE, 1);
}

void lily_push_empty_variant(lily_state *s, uint16_t id)
{
    PUSH_PREAMBLE
    SET_TARGET(id | VAL_IS_ENUM, container, NULL);
}

lily_container_val *lily_push_failure(lily_state *s)
{
    PUSH_CONTAINER(LILY_FAILURE_ID, VAL_IS_ENUM, 1);
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

    SET_TARGET(LILY_FILE_ID | VAL_IS_DEREFABLE, file, filev);
}

lily_foreign_val *lily_push_foreign(lily_state *s, uint16_t id,
        lily_destroy_func func, size_t size)
{
    PUSH_PREAMBLE
    lily_foreign_val *fv = lily_malloc(size * sizeof(*fv));
    fv->refcount = 1;
    fv->class_id = id;
    fv->destroy_func = func;

    SET_TARGET(id | VAL_IS_DEREFABLE | VAL_IS_FOREIGN, foreign, fv);
    return fv;
}

lily_hash_val *lily_push_hash_integer(lily_state *s, int size)
{
    PUSH_PREAMBLE
    lily_hash_val *h = lily_new_hash_integer_raw(size);
    SET_TARGET(LILY_HASH_ID | VAL_IS_DEREFABLE, hash, h);
    return h;
}

lily_hash_val *lily_push_hash_like(lily_state *s, lily_hash_val *other,
        int size)
{
    PUSH_PREAMBLE
    lily_hash_val *h = lily_new_hash_like_raw(other, size);
    SET_TARGET(LILY_HASH_ID | VAL_IS_DEREFABLE, hash, h);
    return h;
}

lily_hash_val *lily_push_hash_string(lily_state *s, int size)
{
    PUSH_PREAMBLE
    lily_hash_val *h = lily_new_hash_string_raw(size);
    SET_TARGET(LILY_HASH_ID | VAL_IS_DEREFABLE, hash, h);
    return h;
}

lily_container_val *lily_push_instance(lily_state *s, uint16_t id,
        uint32_t size)
{
    PUSH_CONTAINER(id, VAL_IS_INSTANCE, size);
}

void lily_push_integer(lily_state *s, int64_t v)
{
    PUSH_PREAMBLE
    SET_TARGET(LILY_INTEGER_ID, integer, v);
}

lily_container_val *lily_push_list(lily_state *s, int size)
{
    PUSH_CONTAINER(LILY_LIST_ID, 0, size);
}

lily_container_val *lily_push_some(lily_state *s)
{
    PUSH_CONTAINER(LILY_SOME_ID, VAL_IS_ENUM, 1);
}

lily_container_val *lily_push_success(lily_state *s)
{
    PUSH_CONTAINER(LILY_SUCCESS_ID, VAL_IS_ENUM, 1);
}

lily_container_val *lily_push_super(lily_state *s, uint16_t id,
        uint32_t initial)
{
    lily_value *v = s->call_chain->return_target;

    if (v->flags & VAL_IS_INSTANCE) {
        lily_container_val *pending_instance = v->value.container;
        if (pending_instance->instance_ctor_need != 0) {
            pending_instance->instance_ctor_need = 0;
            lily_push_value(s, v);
            return pending_instance;
        }
    }

    return lily_push_instance(s, id, initial);
}

void lily_push_string(lily_state *s, const char *source)
{
    PUSH_PREAMBLE
    size_t len = strlen(source);
    char *buffer = lily_malloc((len + 1) * sizeof(*buffer));
    strcpy(buffer, source);

    lily_string_val *sv = new_sv(buffer, len);

    SET_TARGET(LILY_STRING_ID | VAL_IS_DEREFABLE, string, sv);
}

void lily_push_string_sized(lily_state *s, const char *source, int len)
{
    PUSH_PREAMBLE
    char *buffer = lily_malloc((len + 1) * sizeof(*buffer));
    memcpy(buffer, source, len);
    buffer[len] = '\0';

    lily_string_val *sv = new_sv(buffer, len);

    SET_TARGET(LILY_STRING_ID | VAL_IS_DEREFABLE, string, sv);
}

lily_container_val *lily_push_tuple(lily_state *s, int size)
{
    PUSH_CONTAINER(LILY_TUPLE_ID, 0, size);
}

void lily_push_unit(lily_state *s)
{
    PUSH_PREAMBLE
    SET_TARGET(LILY_UNIT_ID, integer, 0);
}

void lily_push_value(lily_state *s, lily_value *v)
{
    PUSH_PREAMBLE
    if (v->flags & VAL_IS_DEREFABLE)
        v->value.generic->refcount++;

    target->flags = v->flags;
    target->value = v->value;
}

lily_container_val *lily_push_variant(lily_state *s, uint16_t id, int size)
{
    PUSH_CONTAINER(id, VAL_IS_ENUM, size);
}

#define RETURN_PREAMBLE \
lily_value *target = s->call_chain->return_target; \
if (target->flags & VAL_IS_DEREFABLE) \
    lily_deref(target);

void lily_return_boolean(lily_state *s, int v)
{
    RETURN_PREAMBLE
    SET_TARGET(LILY_BOOLEAN_ID, integer, v);
}

void lily_return_byte(lily_state *s, uint8_t v)
{
    RETURN_PREAMBLE
    SET_TARGET(LILY_BYTE_ID, integer, v);
}

void lily_return_double(lily_state *s, double v)
{
    RETURN_PREAMBLE
    SET_TARGET(LILY_DOUBLE_ID, doubleval, v);
}

void lily_return_integer(lily_state *s, int64_t v)
{
    RETURN_PREAMBLE
    SET_TARGET(LILY_INTEGER_ID, integer, v);
}

void lily_return_none(lily_state *s)
{
    RETURN_PREAMBLE
    SET_TARGET(LILY_NONE_ID | VAL_IS_ENUM, container, NULL);
}

void lily_return_super(lily_state *s)
{
    lily_value *target = s->call_chain->return_target;
    lily_value *top = *(s->call_chain->top - 1);

    if (target->flags & VAL_IS_INSTANCE &&
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
    SET_TARGET(LILY_UNIT_ID, container, NULL);
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

/* This raises an error in the vm that won't have a proper value backing it. The
   id should be the id of some exception class. This may run a faux dynaload of
   the error, so that printing has a class name to go by. */
static void vm_error(lily_vm_state *vm, uint8_t id, const char *message)
{
    lily_class *c = vm->class_table[id];
    if (c == NULL) {
        /* What this does is to kick parser's exception bootstrapping machinery
           into gear in order to load the exception that's needed. This is
           unfortunate, but the vm doesn't have a sane and easy way to properly
           build classes here. */
        c = lily_dynaload_exception(vm->parser,
                names[id - LILY_EXCEPTION_ID]);

        /* The above will store at least one new function. It's extremely rare,
           but possible, for that to trigger a grow of symtab's literals. If
           realloc moves the underlying data, then vm->readonly_table will be
           invalid. Make sure that doesn't happen. */
        vm->readonly_table = vm->parser->symtab->literals->data;
        vm->class_table[id] = c;
    }

    lily_raise_class(vm->raiser, c, message);
}

#define LILY_ERROR(err, id) \
void lily_##err##Error(lily_vm_state *vm, const char *fmt, ...) \
{ \
    lily_msgbuf *msgbuf = vm->raiser->aux_msgbuf; \
 \
    lily_mb_flush(msgbuf); \
    va_list var_args; \
    va_start(var_args, fmt); \
    lily_mb_add_fmt_va(msgbuf, fmt, var_args); \
    va_end(var_args); \
 \
    vm_error(vm, id, lily_mb_get(msgbuf)); \
}

LILY_ERROR(DivisionByZero, LILY_DBZERROR_ID)
LILY_ERROR(Index,          LILY_INDEXERROR_ID)
LILY_ERROR(IO,             LILY_IOERROR_ID)
LILY_ERROR(Key,            LILY_KEYERROR_ID)
LILY_ERROR(Runtime,        LILY_RUNTIMEERROR_ID)
LILY_ERROR(Value,          LILY_VALUEERROR_ID)

/* Raise KeyError with 'key' as the value of the message. */
static void key_error(lily_vm_state *vm, lily_value *key, uint16_t line_num)
{
    vm->pending_line = line_num;

    lily_msgbuf *msgbuf = vm->raiser->aux_msgbuf;

    if (key->class_id == LILY_STRING_ID)
        lily_mb_escape_add_str(msgbuf, key->value.string->string);
    else
        lily_mb_add_fmt(msgbuf, "%d", key->value.integer);

    vm_error(vm, LILY_KEYERROR_ID, lily_mb_get(msgbuf));
}

/* Raise IndexError, noting that 'bad_index' is, well, bad. */
static void boundary_error(lily_vm_state *vm, int bad_index, uint16_t line_num)
{
    vm->pending_line = line_num;

    lily_msgbuf *msgbuf = vm->raiser->aux_msgbuf;
    lily_mb_flush(msgbuf);
    lily_mb_add_fmt(msgbuf, "Subscript index %d is out of range.",
            bad_index);

    vm_error(vm, LILY_INDEXERROR_ID, lily_mb_get(msgbuf));
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

    lily_move_list_f(MOVE_DEREF_NO_GC, vm->call_chain->return_target, trace);
}

static void do_print(lily_vm_state *vm, FILE *target, lily_value *source)
{
    if (source->class_id == LILY_STRING_ID)
        fputs(source->value.string->string, target);
    else {
        lily_msgbuf *msgbuf = vm->vm_buffer;
        lily_mb_flush(msgbuf);
        lily_mb_add_value(msgbuf, vm, source);
        fputs(lily_mb_get(msgbuf), target);
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
    lily_file_val *stdout_val = vm->regs_from_main[spot]->value.file;
    if (stdout_val->inner_file == NULL)
        vm_error(vm, LILY_VALUEERROR_ID, "IO operation on closed file.");

    do_print(vm, stdout_val->inner_file, lily_arg_value(vm, 0));
}

void lily_builtin_Dynamic_new(lily_vm_state *vm)
{
    lily_value *input = lily_arg_value(vm, 0);

    lily_container_val *dynamic_val = lily_push_dynamic(vm);
    lily_con_set(dynamic_val, 0, input);

    lily_return_top(vm);
    lily_value_tag(vm, vm->call_chain->return_target);
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
    lily_value **vm_regs = vm->call_chain->start;
    lily_value *rhs_reg;
    int index;
    lily_container_val *ival;

    index = code[2];
    ival = vm_regs[code[3]]->value.container;
    rhs_reg = vm_regs[code[4]];

    lily_value_assign(ival->values[index], rhs_reg);
}

static void do_o_get_property(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->call_chain->start;
    lily_value *result_reg;
    int index;
    lily_container_val *ival;

    index = code[2];
    ival = vm_regs[code[3]]->value.container;
    result_reg = vm_regs[code[4]];

    lily_value_assign(result_reg, ival->values[index]);
}

/* This handles subscript assignment. The index is a register, and needs to be
   validated. */
static void do_o_set_item(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->call_chain->start;
    lily_value *lhs_reg, *index_reg, *rhs_reg;

    lhs_reg = vm_regs[code[2]];
    index_reg = vm_regs[code[3]];
    rhs_reg = vm_regs[code[4]];

    if (lhs_reg->class_id != LILY_HASH_ID) {
        int index_int = index_reg->value.integer;

        if (lhs_reg->class_id == LILY_BYTESTRING_ID) {
            lily_string_val *bytev = lhs_reg->value.string;
            if (index_int < 0) {
                int new_index = bytev->size + index_int;
                if (new_index < 0)
                    boundary_error(vm, index_int, code[1]);

                index_int = new_index;
            }
            else if (index_int >= bytev->size)
                boundary_error(vm, index_int, code[1]);

            bytev->string[index_int] = (char)rhs_reg->value.integer;
        }
        else {
            /* List and Tuple have the same internal representation. */
            lily_container_val *list_val = lhs_reg->value.container;

            if (index_int < 0) {
                int new_index = list_val->num_values + index_int;
                if (new_index < 0)
                    boundary_error(vm, index_int, code[1]);

                index_int = new_index;
            }
            else if (index_int >= list_val->num_values)
                boundary_error(vm, index_int, code[1]);

            lily_value_assign(list_val->values[index_int], rhs_reg);
        }
    }
    else
        lily_hash_set(lhs_reg->value.hash, index_reg, rhs_reg);
}

/* This handles subscript access. The index is a register, and needs to be
   validated. */
static void do_o_get_item(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->call_chain->start;
    lily_value *lhs_reg, *index_reg, *result_reg;

    lhs_reg = vm_regs[code[2]];
    index_reg = vm_regs[code[3]];
    result_reg = vm_regs[code[4]];

    if (lhs_reg->class_id != LILY_HASH_ID) {
        int index_int = index_reg->value.integer;

        if (lhs_reg->class_id == LILY_BYTESTRING_ID) {
            lily_string_val *bytev = lhs_reg->value.string;
            if (index_int < 0) {
                int new_index = bytev->size + index_int;
                if (new_index < 0)
                    boundary_error(vm, index_int, code[1]);

                index_int = new_index;
            }
            else if (index_int >= bytev->size)
                boundary_error(vm, index_int, code[1]);

            lily_move_byte(result_reg, (uint8_t) bytev->string[index_int]);
        }
        else {
            /* List and Tuple have the same internal representation. */
            lily_container_val *list_val = lhs_reg->value.container;

            if (index_int < 0) {
                int new_index = list_val->num_values + index_int;
                if (new_index < 0)
                    boundary_error(vm, index_int, code[1]);

                index_int = new_index;
            }
            else if (index_int >= list_val->num_values)
                boundary_error(vm, index_int, code[1]);

            lily_value_assign(result_reg, list_val->values[index_int]);
        }
    }
    else {
        lily_value *elem = lily_hash_get(lhs_reg->value.hash, index_reg);

        /* Give up if the key doesn't exist. */
        if (elem == NULL)
            key_error(vm, index_reg, code[1]);

        lily_value_assign(result_reg, elem);
    }
}

static void do_o_build_hash(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->call_chain->start;
    int i, num_values;
    lily_value *result, *key_reg, *value_reg;

    int id = code[2];
    num_values = code[3];
    result = vm_regs[code[4 + num_values]];

    lily_hash_val *hash_val;
    if (id == LILY_STRING_ID)
        hash_val = lily_new_hash_string_raw(num_values / 2);
    else
        hash_val = lily_new_hash_integer_raw(num_values / 2);

    lily_move_hash_f(MOVE_DEREF_SPECULATIVE, result, hash_val);

    for (i = 0;
         i < num_values;
         i += 2) {
        key_reg = vm_regs[code[4 + i]];
        value_reg = vm_regs[code[4 + i + 1]];

        lily_hash_set(hash_val, key_reg, value_reg);
    }
}

/* Lists and tuples are effectively the same thing internally, since the list
   value holds proper values. This is used primarily to do as the name suggests.
   However, variant types are also tuples (but with a different name). */
static void do_o_build_list_tuple(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->call_chain->start;
    int num_elems = code[2];
    lily_value *result = vm_regs[code[3+num_elems]];
    lily_container_val *lv;

    if (code[0] == o_build_list) {
        lv = new_container(LILY_LIST_ID, num_elems);
        lily_move_list_f(MOVE_DEREF_SPECULATIVE, result, lv);
    }
    else {
        lv = (lily_container_val *)new_container(LILY_TUPLE_ID, num_elems);
        lily_move_tuple_f(MOVE_DEREF_SPECULATIVE, result, (lily_container_val *)lv);
    }

    lily_value **elems = lv->values;

    int i;
    for (i = 0;i < num_elems;i++) {
        lily_value *rhs_reg = vm_regs[code[3+i]];
        lily_value_assign(elems[i], rhs_reg);
    }
}

static void do_o_build_enum(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->call_chain->start;
    int variant_id = code[2];
    int count = code[3];
    lily_value *result = vm_regs[code[code[3] + 4]];

    lily_container_val *ival = new_container(variant_id, count);
    lily_value **slots = ival->values;

    lily_move_variant_f(MOVE_DEREF_SPECULATIVE, result, ival);

    int i;
    for (i = 0;i < count;i++) {
        lily_value *rhs_reg = vm_regs[code[4+i]];
        lily_value_assign(slots[i], rhs_reg);
    }
}

/* This raises a user-defined exception. The emitter has verified that the thing
   to be raised is raiseable (extends Exception). */
static void do_o_raise(lily_vm_state *vm, lily_value *exception_val)
{
    /* The Exception class has values[0] as the message, values[1] as the
       container for traceback. */

    lily_container_val *ival = exception_val->value.container;
    char *message = ival->values[0]->value.string->string;
    lily_class *raise_cls = vm->class_table[ival->class_id];

    /* There's no need for a ref/deref here, because the gc cannot trigger
       foreign stack unwind and/or exception capture. */
    vm->exception_value = exception_val;
    lily_raise_class(vm->raiser, raise_cls, message);
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
    lily_value **vm_regs = vm->call_chain->start;
    lily_value *result = vm_regs[code[3]];
    lily_class *instance_class = vm->class_table[cls_id];

    total_entries = instance_class->prop_count;

    /* Is the caller a superclass building an instance already? */
    lily_value *pending_value = vm->call_chain->return_target;
    if (pending_value->flags & VAL_IS_INSTANCE) {
        lily_container_val *cv = pending_value->value.container;

        if (cv->instance_ctor_need) {
            cv->instance_ctor_need--;
            lily_value_assign(result, pending_value);
            return;
        }
    }

    lily_container_val *iv = new_container(cls_id, total_entries);
    iv->instance_ctor_need = instance_class->inherit_depth;

    if (code[0] == o_new_instance_speculative)
        lily_move_instance_f(MOVE_DEREF_SPECULATIVE, result, iv);
    else {
        lily_move_instance_f(MOVE_DEREF_NO_GC, result, iv);
        if (code[0] == o_new_instance_tagged)
            lily_value_tag(vm, result);
    }
}

static void do_o_interpolation(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->call_chain->start;
    int count = code[2];
    lily_msgbuf *vm_buffer = vm->vm_buffer;
    lily_mb_flush(vm_buffer);

    int i;
    for (i = 0;i < count;i++) {
        lily_value *v = vm_regs[code[3 + i]];
        lily_mb_add_value(vm_buffer, vm, v);
    }

    lily_value *result_reg = vm_regs[code[3 + i]];

    lily_string_val *sv = lily_new_string_raw(lily_mb_get(vm_buffer));
    lily_move_string(result_reg, sv);
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
static lily_value **do_o_create_closure(lily_vm_state *vm, uint16_t *code)
{
    int count = code[2];
    lily_value *result = vm->call_chain->start[code[3]];

    lily_function_val *last_call = vm->call_chain->function;

    lily_function_val *closure_func = new_function_copy(last_call);

    lily_value **upvalues = lily_malloc(sizeof(*upvalues) * count);

    /* Cells are initially NULL so that o_set_upvalue knows to copy a new value
       into a cell. */
    int i;
    for (i = 0;i < count;i++)
        upvalues[i] = NULL;

    closure_func->num_upvalues = count;
    closure_func->upvalues = upvalues;

    /* Put the closure into a register so that the gc has an easy time of
       finding it. This also helps to ensure it goes away in a more predictable
       manner, in case there aren't many gc objects. */
    lily_move_function_f(MOVE_DEREF_NO_GC, result, closure_func);
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
static void do_o_create_function(lily_vm_state *vm, uint16_t *code)
{
    lily_value **vm_regs = vm->call_chain->start;
    lily_function_val *input_closure = vm->call_chain->function;

    lily_value *target = vm->readonly_table[code[1]];
    lily_function_val *target_func = target->value.function;

    lily_value *result_reg = vm_regs[code[2]];
    lily_function_val *new_closure = new_function_copy(target_func);

    copy_upvalues(new_closure, input_closure);

    uint16_t *locals = new_closure->locals;
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

    lily_move_function_f(MOVE_DEREF_SPECULATIVE, result_reg, new_closure);
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

    lily_msgbuf *msgbuf = lily_get_clean_msgbuf(vm);
    lily_container_val *lv = new_container(LILY_LIST_ID, depth);

    /* The call chain goes from the most recent to least. Work around that by
       allocating elements in reverse order. It's safe to do this because
       nothing in this loop can trigger the gc. */
    for (i = depth;
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

        const char *str = lily_mb_sprintf(msgbuf, "%s:%s from %s%s%s", path,
                line, class_name, separator, name);

        lily_string_val *sv = lily_new_string_raw(str);
        lily_move_string(lv->values[i - 1], sv);
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
    lily_container_val *ival = new_container(raised_cls->id, 2);

    lily_string_val *sv = lily_new_string_raw(raw_message);
    lily_move_string(ival->values[0], sv);

    lily_move_list_f(MOVE_DEREF_NO_GC, ival->values[1], build_traceback_raw(vm));

    lily_move_instance_f(MOVE_DEREF_SPECULATIVE, result, ival);
}

/* This is called when 'raise' raises an error. The traceback property is
   assigned to freshly-made traceback. The other fields of the value are left
   intact, however. */
static void fixup_exception_val(lily_vm_state *vm, lily_value *result)
{
    lily_value_assign(result, vm->exception_value);
    lily_container_val *raw_trace = build_traceback_raw(vm);
    lily_container_val *iv = result->value.container;

    lily_move_list_f(MOVE_DEREF_SPECULATIVE, lily_con_get(iv, 1), raw_trace);
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
    int jump_location, match;
    uint16_t *code;

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
        code = call_frame->function->code;
        /* A try block is done when the next jump is at 0 (because 0 would
           always be going back, which is illogical otherwise). */
        jump_location = catch_iter->code_pos + code[catch_iter->code_pos] - 2;

        while (1) {
            lily_class *catch_class = vm->class_table[code[jump_location + 2]];

            if (lily_class_greater_eq(catch_class, raised_cls)) {
                /* ...So that execution resumes from within the except block. */
                jump_location += 4;
                match = 1;
                break;
            }
            else {
                int move_by = code[jump_location + 3];
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
        code += jump_location;
        if (*code == o_store_exception) {
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
        vm->call_chain = catch_iter->call_frame;
        vm->call_depth = catch_iter->call_frame_depth;
        vm->call_chain->code = code;
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

lily_msgbuf *lily_get_clean_msgbuf(lily_vm_state *vm)
{
    lily_msgbuf *msgbuf = vm->vm_buffer;
    lily_mb_flush(msgbuf);
    return msgbuf;
}

void lily_get_dirty_msgbuf(lily_vm_state *vm, lily_msgbuf **msgbuf)
{
    *msgbuf = vm->vm_buffer;
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
    target_frame->line_num = 0;
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
                sizeof(*vm->class_table) * vm->class_count);
    }

    /* For the first pass, make sure the spots for Exception and its built-in
       children are zero'ed out. This allows vm_error to safely check if an
       exception class has been loaded by testing the class field for being NULL
       (and relies on holes being set aside for these exceptions). */
    if (old_count == 0) {
        int i;
        for (i = LILY_EXCEPTION_ID;i < START_CLASS_ID;i++)
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

/* This must be called before lily_vm_execute if the parser has read any data
   in. This makes sure that __main__ has enough register slots, that the
   vm->readonly_table is set, and that foreign ties are loaded. */
void lily_vm_prep(lily_vm_state *vm, lily_symtab *symtab, lily_function_val *f,
        lily_value **readonly_table)
{
    vm->readonly_table = readonly_table;

    lily_function_val *main_function = f;
    int need = main_function->reg_count + symtab->next_global_id;
    int total = (int)(vm->call_chain->register_end - vm->regs_from_main);

    if (need > total)
        grow_vm_registers(vm, need);

    lily_call_frame *toplevel_frame = vm->call_chain;
    toplevel_frame->top = vm->regs_from_main + symtab->next_global_id;

    lily_call_frame *main_frame = vm->call_chain->next;
    main_frame->start = toplevel_frame->top;
    main_frame->top = vm->regs_from_main + need;
    main_frame->function = main_function;
    main_frame->code = main_function->code;

    vm->call_chain = vm->call_chain->next;
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
    lily_value **vm_regs;
    int i;
    register int64_t for_temp;
    register lily_value *lhs_reg, *rhs_reg, *loop_reg, *step_reg;
    lily_function_val *fval;
    lily_value **upvalues = NULL;

    lily_call_frame *current_frame = vm->call_chain;
    lily_call_frame *next_frame = NULL;

    code = current_frame->function->code;

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
        }
    }

    upvalues = current_frame->function->upvalues;
    vm_regs = vm->call_chain->start;

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
            case o_get_empty_variant:
                lhs_reg = vm_regs[code[3]];

                lily_deref(lhs_reg);

                lhs_reg->value.container = NULL;
                lhs_reg->flags = VAL_IS_ENUM | code[2];
                code += 4;
                break;
            case o_get_integer:
                lhs_reg = vm_regs[code[3]];
                lhs_reg->value.integer = (int16_t)code[2];
                lhs_reg->flags = LILY_INTEGER_ID;
                code += 4;
                break;
            case o_get_boolean:
                lhs_reg = vm_regs[code[3]];
                lhs_reg->value.integer = code[2];
                lhs_reg->flags = LILY_BOOLEAN_ID;
                code += 4;
                break;
            case o_get_byte:
                lhs_reg = vm_regs[code[3]];
                lhs_reg->value.integer = (uint8_t)code[2];
                lhs_reg->flags = LILY_BYTE_ID;
                code += 4;
                break;
            case o_integer_add:
                INTEGER_OP(+)
                break;
            case o_integer_minus:
                INTEGER_OP(-)
                break;
            case o_double_add:
                DOUBLE_OP(+)
                break;
            case o_double_minus:
                DOUBLE_OP(-)
                break;
            case o_is_equal:
                EQUALITY_COMPARE_OP(==)
                break;
            case o_greater:
                COMPARE_OP(>)
                break;
            case o_greater_eq:
                COMPARE_OP(>=)
                break;
            case o_not_eq:
                EQUALITY_COMPARE_OP(!=)
                break;
            case o_jump:
                code += (int16_t)code[1];
                break;
            case o_integer_mul:
                INTEGER_OP(*)
                break;
            case o_double_mul:
                DOUBLE_OP(*)
                break;
            case o_integer_div:
                /* Before doing INTEGER_OP, check for a division by zero. This
                   will involve some redundant checking of the rhs, but better
                   than dumping INTEGER_OP's contents here or rewriting
                   INTEGER_OP for the special case of division. */
                rhs_reg = vm_regs[code[3]];
                if (rhs_reg->value.integer == 0) {
                    vm->pending_line = code[1];
                    vm_error(vm, LILY_DBZERROR_ID,
                            "Attempt to divide by zero.");
                }
                INTEGER_OP(/)
                break;
            case o_modulo:
                /* x % 0 will do the same thing as x / 0... */
                rhs_reg = vm_regs[code[3]];
                if (rhs_reg->value.integer == 0) {
                    vm->pending_line = code[1];
                    vm_error(vm, LILY_DBZERROR_ID,
                            "Attempt to divide by zero.");
                }

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
                rhs_reg = vm_regs[code[3]];
                if (rhs_reg->value.doubleval == 0) {
                    vm->pending_line = code[1];
                    vm_error(vm, LILY_DBZERROR_ID,
                            "Attempt to divide by zero.");
                }

                DOUBLE_OP(/)
                break;
            case o_jump_if:
                lhs_reg = vm_regs[code[2]];
                {
                    int id = lhs_reg->class_id;
                    int result;

                    if (id == LILY_INTEGER_ID || id == LILY_BOOLEAN_ID)
                        result = (lhs_reg->value.integer == 0);
                    else if (id == LILY_STRING_ID)
                        result = (lhs_reg->value.string->size == 0);
                    else if (id == LILY_LIST_ID)
                        result = (lhs_reg->value.container->num_values == 0);
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
                        vm_error(vm, LILY_RUNTIMEERROR_ID,
                                "Function call recursion limit reached.");
                    add_call_frame(vm);
                }

                next_frame = current_frame->next;

                i = code[3];
                current_frame->line_num = code[1];
                current_frame->code = code + i + 5;

                next_frame->start = current_frame->top;
                next_frame->top = next_frame->start + i;
                next_frame->function = fval;
                next_frame->line_num = -1;
                next_frame->code = NULL;
                next_frame->return_target = vm_regs[code[i + 4]];

                if (next_frame->top >= next_frame->register_end) {
                    vm->call_chain = next_frame;
                    grow_vm_registers(vm, i + 1);
                }

                lily_foreign_func func = fval->foreign_func;

                /* Prepare the registers for what the function wants. */
                prep_registers(current_frame, code);
                vm_regs = next_frame->start;

                /* !PAST HERE TARGETS THE NEW FRAME! */

                current_frame = next_frame;
                vm->call_chain = current_frame;

                vm->call_depth++;
                func(vm);

                current_frame = current_frame->prev;

                vm_regs = current_frame->start;

                vm->call_chain = current_frame;

                code += 5 + i;
                vm->call_depth--;

                break;
            case o_native_call: {
                fval = vm->readonly_table[code[2]]->value.function;

                native_func_body: ;

                if (current_frame->next == NULL) {
                    if (vm->call_depth > 100)
                        vm_error(vm, LILY_RUNTIMEERROR_ID,
                                "Function call recursion limit reached.");
                    add_call_frame(vm);
                }

                next_frame = current_frame->next;

                i = code[3];
                current_frame->line_num = code[1];
                current_frame->code = code + i + 5;

                next_frame->start = current_frame->top;
                next_frame->top = next_frame->start + fval->reg_count;
                next_frame->function = fval;
                next_frame->line_num = -1;
                next_frame->code = NULL;
                next_frame->return_target = vm_regs[code[i + 4]];

                if (next_frame->top >= next_frame->register_end) {
                    vm->call_chain = next_frame;
                    grow_vm_registers(vm, fval->reg_count);
                }

                /* Prepare the registers for what the function wants. */
                prep_registers(current_frame, code);

                vm_regs = next_frame->start;

                /* !PAST HERE TARGETS THE NEW FRAME! */

                current_frame = current_frame->next;
                vm->call_chain = current_frame;

                vm->call_depth++;
                code = fval->code;
                upvalues = fval->upvalues;

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
                rhs_reg->flags = LILY_INTEGER_ID;
                rhs_reg->value.integer = -(lhs_reg->value.integer);
                code += 4;
                break;
            case o_return_unit:
                lily_move_unit(current_frame->return_target);
                goto return_common;

            case o_return_val:
                lhs_reg = current_frame->return_target;
                rhs_reg = vm_regs[code[2]];
                lily_value_assign(lhs_reg, rhs_reg);

                return_common: ;

                current_frame = current_frame->prev;
                vm->call_chain = current_frame;
                vm->call_depth--;

                vm_regs = current_frame->start;
                upvalues = current_frame->function->upvalues;
                code = current_frame->code;
                break;
            case o_get_global:
                rhs_reg = vm->regs_from_main[code[2]];
                lhs_reg = vm_regs[code[3]];

                lily_value_assign(lhs_reg, rhs_reg);
                code += 4;
                break;
            case o_set_global:
                rhs_reg = vm_regs[code[2]];
                lhs_reg = vm->regs_from_main[code[3]];

                lily_value_assign(lhs_reg, rhs_reg);
                code += 4;
                break;
            case o_assign:
                rhs_reg = vm_regs[code[2]];
                lhs_reg = vm_regs[code[3]];

                lily_value_assign(lhs_reg, rhs_reg);
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
                code += code[3] + 5;
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
            case o_create_function:
                do_o_create_function(vm, code);
                code += 3;
                break;
            case o_set_upvalue:
                lhs_reg = upvalues[code[2]];
                rhs_reg = vm_regs[code[3]];
                if (lhs_reg == NULL)
                    upvalues[code[2]] = make_cell_from(rhs_reg);
                else
                    lily_value_assign(lhs_reg, rhs_reg);

                code += 4;
                break;
            case o_get_upvalue:
                lhs_reg = vm_regs[code[3]];
                rhs_reg = upvalues[code[2]];
                lily_value_assign(lhs_reg, rhs_reg);
                code += 4;
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

                vm->catch_chain = vm->catch_chain->next;
                code += 3;
                break;
            }
            case o_pop_try:
                vm->catch_chain = vm->catch_chain->prev;

                code++;
                break;
            case o_raise:
                vm->pending_line = code[1];
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
            case o_jump_if_not_class:
                lhs_reg = vm_regs[code[1]];
                i = code[2];

                if (lhs_reg->class_id == i)
                    code += 4;
                else
                    code += code[3];

                break;
            case o_create_closure:
                do_o_create_closure(vm, code);
                upvalues = current_frame->function->upvalues;
                code += 4;
                break;
            case o_for_setup:
                /* lhs_reg is the start, rhs_reg is the stop. */
                lhs_reg = vm_regs[code[2]];
                rhs_reg = vm_regs[code[3]];
                step_reg = vm_regs[code[4]];
                loop_reg = vm_regs[code[5]];

                if (step_reg->value.integer == 0)
                    vm_error(vm, LILY_VALUEERROR_ID,
                               "for loop step cannot be 0.");

                /* Do a negative step to offset falling into o_for_loop. */
                loop_reg->value.integer =
                        lhs_reg->value.integer - step_reg->value.integer;
                lhs_reg->value.integer = loop_reg->value.integer;
                loop_reg->flags = LILY_INTEGER_ID;

                code += 6;
                break;
            case o_return_from_vm:
                lily_release_jump(vm->raiser);
                return;
            default:
                return;
        }
    }
}
