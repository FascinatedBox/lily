#ifndef LILY_VM_H
# define LILY_VM_H

# include "lily_raiser.h"
# include "lily_symtab.h"

typedef struct {
    lily_method_val *method;
    signed int return_reg;
    /* How many registers this call uses. This is used to fix the vm's register
       stack after a call. */
    int regs_used;
    uintptr_t *code;
    int code_pos;
    int line_num;
} lily_vm_stack_entry;

typedef struct {
    char *data;
    int data_size;
} lily_vm_stringbuf;

typedef struct lily_vm_state_t {
    lily_value **vm_regs;
    lily_value **regs_from_main;
    int num_registers;
    int max_registers;

    /* The function that raised the current error, or NULL if it wasn't from a
       function call. */
    lily_function_val *err_function;

    lily_vm_stack_entry **method_stack;
    int method_stack_pos;
    int method_stack_size;

    /* A linked list of entries that are currently being used. */
    lily_gc_entry *gc_live_entries;
    /* A linked list of entries not currently used. Entries which have their
       values swept are put here. This is checked before making a new node. */
    lily_gc_entry *gc_spare_entries;
    /* How many entries are in ->gc_live_entries. If this is >= ->gc_threshold,
       then the gc is triggered when there is an attempt to attach a gc_entry
       to a value. */
    int gc_live_entry_count;
    /* How many entries to allow in ->gc_live_entries before doing a sweep. */
    int gc_threshold;
    /* An always-increasing value indicating the current pass, used to determine
       if an entry has been seen. An entry is visible if
       'entry->last_pass == gc_pass' */
    int gc_pass;

    /* This is the default signature used when created new registers. This is
       used because it isn't refcounted. */
    lily_sig *integer_sig;

    int prep_id_start;
    lily_var *prep_var_start;

    char *sipkey;
    /* This lets the vm know that it was in a function when an error is raised
       so it can set err_function properly. Runners should only check
       err_function. */
    int in_function;
    lily_vm_stringbuf *string_buffer;
    lily_raiser *raiser;
    void *data;
    lily_var *main;
} lily_vm_state;

lily_vm_state *lily_new_vm_state(lily_raiser *, void *);
void lily_free_vm_state(lily_vm_state *);
void lily_vm_prep(lily_vm_state *, lily_symtab *);
void lily_vm_execute(lily_vm_state *);
void lily_vm_free_registers(lily_vm_state *);
void lily_assign_value(lily_vm_state *, lily_value *, lily_value *);
uint64_t lily_calculate_siphash(char *, lily_value *);

#endif
