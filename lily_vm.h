#ifndef LILY_VM_H
# define LILY_VM_H

# include "lily_raiser.h"
# include "lily_symtab.h"

typedef struct {
    lily_value value;
    int flags;
    lily_sym *sym;
} lily_saved_val;

typedef struct {
    lily_sym *method;
    lily_sym *ret;
    uintptr_t *code;
    int code_pos;
    int line_num;
} lily_vm_stack_entry;

typedef struct {
    lily_saved_val *saved_values;
    int val_pos;
    int val_size;

    lily_vm_stack_entry **method_stack;
    int method_stack_pos;
    int method_stack_size;

    lily_raiser *raiser;
    lily_var *main;
} lily_vm_state;

lily_vm_state *lily_new_vm_state(lily_raiser *);
void lily_free_vm_state(lily_vm_state *);
void lily_vm_execute(lily_vm_state *);

#endif
