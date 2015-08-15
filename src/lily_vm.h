#ifndef LILY_VM_H
# define LILY_VM_H

# include "lily_raiser.h"
# include "lily_symtab.h"
# include "lily_type_system.h"

struct lily_base_seed_;

typedef struct lily_call_frame_ {
    lily_function_val *function;
    signed int return_reg;
    /* How many registers this call uses. This is used to fix the vm's register
       stack after a call. */
    int regs_used;
    uint16_t *code;
    int code_pos;
    int line_num;

    uint32_t offset_to_main;

    lily_value **upvalues;

    /* This is set to the value of 'self' within the ::new of a class. The
       instruction o_new_instance uses this to determine if a constructor is
       being executed from a higher-up class. If that is the case, then the
       subclass uses the value of the higher-up class. */
    lily_instance_val *build_value;

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
    uint32_t dispatch_restore;
    lily_jump_link *jump_entry;

    struct lily_vm_catch_entry_ *next;
    struct lily_vm_catch_entry_ *prev;
} lily_vm_catch_entry;

typedef struct lily_vm_state_ {
    lily_value **vm_regs;
    lily_value **regs_from_main;
    lily_call_frame *call_chain;

    lily_tie **readonly_table;

    /* A linked list of entries that are currently being used. */
    lily_gc_entry *gc_live_entries;
    /* A linked list of entries not currently used. Entries which have their
       values swept are put here. This is checked before making a new node. */

    lily_gc_entry *gc_spare_entries;

    uint32_t num_registers;

    /* This is how many registers the vm has made, minus 2. This is used along
       with the number of registers to determine */
    uint32_t offset_max_registers;
    uint32_t true_max_registers;
    uint32_t pad;

    uint32_t readonly_count;

    uint32_t call_depth;

    /* How many entries are in ->gc_live_entries. If this is >= ->gc_threshold,
       then the gc is triggered when there is an attempt to attach a gc_entry
       to a value. */
    uint16_t gc_live_entry_count;
    /* How many entries to allow in ->gc_live_entries before doing a sweep. */
    uint16_t gc_threshold;
    /* An always-increasing value indicating the current pass, used to determine
       if an entry has been seen. An entry is visible if
       'entry->last_pass == gc_pass' */
    uint32_t gc_pass;

    /* This is the default type used when created new registers. This is
       used because it isn't refcounted. */
    lily_type *integer_type;

    uint64_t prep_id_start;

    /* Most of the stack entries will be native functions, with the lowest
       being __main__. __main__ has o_return_from_vm at the end, so native
       functions are fine.
       Foreign functions do not have native code, and they need to bail out of
       the vm. So the vm lies and says this "foreign code" as its code. This
       contains a o_return_from_vm as its instruction. */
    uint16_t *foreign_code;

    char *sipkey;

    lily_vm_catch_entry *catch_top;
    lily_vm_catch_entry *catch_chain;

    /* This is used when building traceback, as the inner values of traceback
       are full values which need types. */
    lily_type *traceback_type;

    struct lily_parse_state_ *parser;
    lily_msgbuf *vm_buffer;
    lily_type_system *ts;
    lily_symtab *symtab;
    lily_raiser *raiser;
    void *data;
    lily_var *main;
} lily_vm_state;

lily_vm_state *lily_new_vm_state(lily_options *, lily_raiser *);
void lily_free_vm(lily_vm_state *);
void lily_vm_prep(lily_vm_state *, lily_symtab *);
void lily_vm_execute(lily_vm_state *);
lily_hash_elem *lily_lookup_hash_elem(lily_hash_val *, uint64_t, lily_value *);
void lily_assign_value(lily_vm_state *, lily_value *, lily_value *);
void lily_move_raw_value(lily_vm_state *, lily_value *, lily_raw_value);
uint64_t lily_calculate_siphash(char *, lily_value *);
void lily_process_format_string(lily_vm_state *, uint16_t *);

void lily_add_gc_item(lily_vm_state *, lily_type *, lily_generic_gc_val *);

lily_value *lily_foreign_call(lily_vm_state *, int *, lily_type *, lily_value *,
        int, ...);

void lily_vm_set_error(lily_vm_state *, const struct lily_base_seed_ *,
        const char *);
void lily_vm_raise_prepared(lily_vm_state *);
void lily_vm_module_raise(lily_vm_state *, const struct lily_base_seed_ *,
        const char *);
#endif
