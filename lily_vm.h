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

typedef struct lily_vm_state_t {
    lily_vm_register **vm_regs;
    lily_vm_register **regs_from_main;
    int num_registers;
    int max_registers;

    /* The function that raised the current error, or NULL if it wasn't from a
       function call. */
    lily_function_val *err_function;

    lily_vm_stack_entry **method_stack;
    int method_stack_pos;
    int method_stack_size;

    /* This is the default signature used when created new registers. This is
       used because it isn't refcounted. */
    lily_sig *integer_sig;

    char *sipkey;
    /* This lets the vm know that it was in a function when an error is raised
       so it can set err_function properly. Runners should only check
       err_function. */
    int in_function;
    lily_raiser *raiser;
    lily_var *main;
} lily_vm_state;

lily_vm_state *lily_new_vm_state(lily_raiser *);
void lily_free_vm_state(lily_vm_state *);
void lily_vm_prep(lily_vm_state *, lily_symtab *);
void lily_vm_execute(lily_vm_state *);

#endif
