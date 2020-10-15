#ifndef LILY_VM_H
# define LILY_VM_H

# include "lily.h"
# include "lily_raiser.h"
# include "lily_symtab.h"

typedef struct lily_call_frame_ {
    /* This frame's registers start here. */
    lily_value **start;
    /* One past the last register used (next frame starts here). */
    lily_value **top;
    /* Starts at the last register available. Grow when top == end. */
    lily_value **register_end;
    /* With foreign functions, this is always set to an instruction to leave the
       vm.

       For all native functions except the top one, this is where that frame
       will continue when it has control again. On the other hand, the top frame
       only sets this field if the vm is executing an instruction that might
       raise an error.

       Since emitter writes line numbers at the end of instructions, the line
       number of any native frame can be obtained through code[-1]. */
    uint16_t *code;
    lily_function_val *function;
    /* A value from the previous frame to return back into. */
    lily_value *return_target;

    struct lily_call_frame_ *prev;
    struct lily_call_frame_ *next;
} lily_call_frame;

typedef enum {
    catch_native,
    catch_callback
} lily_catch_kind;

typedef struct lily_vm_catch_entry_ {
    lily_call_frame *call_frame;
    int code_pos;
    uint32_t call_frame_depth;
    lily_catch_kind catch_kind : 32;

    union {
        lily_jump_link *jump_entry;
        lily_error_callback_func callback_func;
    };

    struct lily_vm_catch_entry_ *next;
    struct lily_vm_catch_entry_ *prev;
} lily_vm_catch_entry;

/* This is shared by the vm and any coroutines that are created within it. It
   serves two purposes:
   * One, that parse updates do not leave a coroutine with stale tables.
   * Two, that there is one set of gc information with 'regs_from_main'
     belonging to the first state. The first state acts as a main 'thread'. */
typedef struct lily_global_state_ {
    lily_value **regs_from_main;

    lily_value **readonly_table;
    lily_class **class_table;
    uint32_t class_count;
    uint32_t readonly_count;

    /* A linked list of entries that should be findable from a register. */
    lily_gc_entry *gc_live_entries;

    /* A linked list of entries not currently in use. */
    lily_gc_entry *gc_spare_entries;

    /* How many entries are in ->gc_live_entries. If this is >= ->gc_threshold,
       then the gc is triggered when there is an attempt to attach a gc_entry
       to a value. */
    uint32_t gc_live_entry_count;
    /* How many entries to allow in ->gc_live_entries before doing a sweep. */
    uint32_t gc_threshold;
    /* An always-increasing value indicating the current pass, used to determine
       if an entry has been seen. An entry is visible if
       'entry->last_pass == gc_pass' */
    uint32_t gc_pass;

    /* If the current gc sweep does not free anything, this is how much that
       the threshold is multiplied by to increase it. */
    uint32_t gc_multiplier;

    struct lily_vm_state_ *first_vm;

    /* This is used to dynaload exceptions when absolutely necessary. */
    struct lily_parse_state_ *parser;
} lily_global_state;

/* A virtual machine capable of executing code, and the underlying struct behind
   lily_state. When a new coroutine is created, it creates another one of these
   within the parent state wrapped up in a value. */
typedef struct lily_vm_state_ {
    lily_value **register_root;

    uint32_t call_depth;

    uint32_t depth_max;

    lily_call_frame *call_chain;

    lily_global_state *gs;

    lily_vm_catch_entry *catch_chain;

    /* If a proper value is being raised (currently only the `raise` keyword),
       then this is the value raised. Otherwise, this is NULL. Since exception
       capture sets this to NULL when successful, raises of non-proper values do
       not need to do anything. */
    lily_value *exception_value;

    lily_class *exception_cls;

    /* This buffer is used as an intermediate storage for String values. */
    lily_msgbuf *vm_buffer;

    lily_raiser *raiser;
} lily_vm_state;

lily_vm_state *lily_new_vm_state(lily_raiser *);
void lily_rewind_vm(lily_vm_state *);
void lily_free_vm(lily_vm_state *);

lily_vm_state *lily_vm_coroutine_build(lily_vm_state *, uint16_t);
void lily_vm_coroutine_call_prep(lily_vm_state *, uint16_t);
void lily_vm_coroutine_resume(lily_vm_state *, lily_coroutine_val *,
        lily_value *);

void lily_vm_execute(lily_vm_state *);

void lily_vm_ensure_class_table(lily_vm_state *, uint16_t);
void lily_vm_add_class_unchecked(lily_vm_state *, lily_class *);

#endif
