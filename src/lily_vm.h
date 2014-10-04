#ifndef LILY_VM_H
# define LILY_VM_H

# include "lily_raiser.h"
# include "lily_symtab.h"

typedef struct {
    lily_function_val *function;
    signed int return_reg;
    /* How many registers this call uses. This is used to fix the vm's register
       stack after a call. */
    int regs_used;
    uint16_t *code;
    int code_pos;
    int line_num;
} lily_vm_stack_entry;

typedef struct {
    char *data;
    int data_size;
} lily_vm_stringbuf;

typedef struct lily_vm_catch_entry_t {
    lily_vm_stack_entry *stack_entry;
    int entry_depth;
    int code_pos;

    struct lily_vm_catch_entry_t *next;
    struct lily_vm_catch_entry_t *prev;
} lily_vm_catch_entry;

typedef struct lily_vm_state_t {
    lily_value **vm_regs;
    lily_value **regs_from_main;
    int num_registers;
    int max_registers;

    lily_literal **literal_table;
    int literal_count;

    lily_var **function_table;
    int function_count;

    lily_vm_stack_entry **function_stack;
    int function_stack_pos;
    int function_stack_size;

    /* Sometimes there's a var in a generic function which has a signature
       that isn't the same as any of the function's parameters. In that case,
       the vm has to get the completed signature by building it. This is used
       for that. */
    lily_sig **resolver_sigs;
    int resolver_sigs_size;

    /* This helps to determine what the proper sigs are for the vars and the
       storages of a generic function. */
    lily_sig **generic_map;
    int generic_map_size;

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
    lily_literal *prep_literal_start;

    /* Most of the stack entries will be native functions, with the lowest
       being __main__. __main__ has o_return_from_vm at the end, so native
       functions are fine.
       Foreign functions do not have native code, and they need to bail out of
       the vm. So the vm lies and says this "foreign code" as its code. This
       contains a o_return_from_vm as its instruction. */
    uint16_t *foreign_code;

    char *sipkey;
    /* This lets the vm know that it was in a function when an error is raised
       so it can set err_function properly. Runners should only check
       err_function. */
    lily_vm_stringbuf *string_buffer;

    lily_vm_catch_entry *catch_top;
    lily_vm_catch_entry *catch_chain;
    int building_error;

    lily_symtab *symtab;
    lily_raiser *raiser;
    void *data;
    lily_var *main;
} lily_vm_state;

lily_vm_state *lily_new_vm_state(lily_raiser *, void *);
void lily_free_vm_state(lily_vm_state *);
void lily_vm_prep(lily_vm_state *, lily_symtab *);
void lily_vm_execute(lily_vm_state *);
void lily_vm_free_registers(lily_vm_state *);
lily_hash_elem *lily_try_lookup_hash_elem(lily_hash_val *, uint64_t,
        lily_value *);
void lily_assign_value(lily_vm_state *, lily_value *, lily_value *);
uint64_t lily_calculate_siphash(char *, lily_value *);

void lily_vm_foreign_call(lily_vm_state *vm);
void lily_vm_foreign_prep(lily_vm_state *, lily_function_val *, lily_value *);
void lily_vm_foreign_load_by_val(lily_vm_state *, int, lily_value *);
lily_value *lily_vm_get_foreign_reg(lily_vm_state *, int);
#endif
