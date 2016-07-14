#ifndef LILY_VM_H
# define LILY_VM_H

# include "lily_raiser.h"
# include "lily_symtab.h"

typedef struct lily_call_frame_ {
    lily_function_val *function;
    lily_value *return_target;
    /* How many registers this call uses. This is used to fix the vm's register
       stack after a call. */
    int regs_used;
    uint16_t *code;
    int code_pos;
    int line_num;

    uint32_t offset_to_main;

    lily_value **upvalues;

    /* This is set to the value of 'self' within the .new of a class. The
       instruction o_new_instance uses this to determine if a constructor is
       being executed from a higher-up class. If that is the case, then the
       subclass uses the value of the higher-up class. */
    lily_value *build_value;

    struct lily_call_frame_ *prev;
    struct lily_call_frame_ *next;
} lily_call_frame;

typedef struct lily_vm_catch_entry_ {
    lily_call_frame *call_frame;
    /* How far away vm->vm_regs (where the locals start) is from
       vm->regs_from_main in the current frame. When catching exceptions, it's
       simpler, safer, and faster to fix vm->vm_regs with this instead of
       attempting to walk the call chain backward to figure out where
       vm->vm_regs will end up. */
    int offset_from_main;
    int code_pos;
    uint32_t call_frame_depth;
    uint32_t pad;
    lily_jump_link *jump_entry;

    struct lily_vm_catch_entry_ *next;
    struct lily_vm_catch_entry_ *prev;
} lily_vm_catch_entry;

typedef struct lily_vm_state_ {
    lily_value **vm_regs;
    lily_value **regs_from_main;

    /* The total number of registers available, minus 2. This is used to check
       for space checks so that there are always 2 registers available for
       foreign calls without needing to check. */
    uint32_t offset_max_registers;

    /* The number of registers currently being used. */
    uint32_t num_registers;

    /* The actual total, for parts like gc collection + register grow. */
    uint32_t true_max_registers;

    uint32_t call_depth;

    lily_call_frame *call_chain;

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

    char *sipkey;

    lily_vm_catch_entry *catch_chain;

    /* If a proper value is being raised (currently only the `raise` keyword),
       then this is the value raised. Otherwise, this is NULL. Since exception
       capture sets this to NULL when successful, raises of non-proper values do
       not need to do anything. */
    lily_value *exception_value;

    /* This buffer is used as an intermediate storage for String values. */
    lily_msgbuf *vm_buffer;

    /* This is used to dynaload exceptions when absolutely necessary. */
    struct lily_parse_state_ *parser;
    lily_symtab *symtab;
    lily_raiser *raiser;
    /* This holds the 'data' blob passed in to the interpreter's options. The
       mod_lily module uses this to hold the request_rec so that server
       functions can fetch it back out. */
    void *data;

    /* If stdout has been dynaloaded, then this is the register that holds
       Lily's stdout. Otherwise, this is NULL. */
    lily_value *stdout_reg;
} lily_vm_state;

struct lily_value_stack_;

void lily_vm_raise(lily_vm_state *, uint8_t, const char *);
void lily_vm_raise_fmt(lily_vm_state *, uint8_t, const char *, ...);

lily_vm_state *lily_new_vm_state(struct lily_options_ *, lily_raiser *);
void lily_free_vm(lily_vm_state *);
void lily_vm_prep(lily_vm_state *, lily_symtab *, lily_value **,
        struct lily_value_stack_ *);
void lily_vm_execute(lily_vm_state *);
uint64_t lily_siphash(lily_vm_state *, lily_value *);
void lily_vm_add_value_to_msgbuf(lily_vm_state *vm, lily_msgbuf *, lily_value *);

void lily_tag_value(lily_vm_state *, lily_value *);

void lily_vm_ensure_class_table(lily_vm_state *, int);
void lily_vm_add_class_unchecked(lily_vm_state *, lily_class *);
void lily_vm_add_class(lily_vm_state *, lily_class *);

lily_value *lily_foreign_call(lily_vm_state *, int *, int, lily_value *,
        int, ...);

#endif
