#ifndef LILY_CLASS_FUNCS_H
# define LILY_CLASS_FUNCS_H

# include "lily_syminfo.h"

int lily_integer_eq(struct lily_vm_state_t *, int *, lily_value *, lily_value *);
int lily_number_eq(struct lily_vm_state_t *, int *, lily_value *, lily_value *);
int lily_string_eq(struct lily_vm_state_t *, int *, lily_value *, lily_value *);
int lily_object_eq(struct lily_vm_state_t *, int *, lily_value *, lily_value *);
int lily_list_eq(struct lily_vm_state_t *, int *, lily_value *, lily_value *);
int lily_hash_eq(struct lily_vm_state_t *, int *, lily_value *, lily_value *);
int lily_generic_eq(struct lily_vm_state_t *, int *, lily_value *, lily_value *);

void lily_gc_list_marker(int, lily_value *);
void lily_gc_object_marker(int, lily_value *);
void lily_gc_hash_marker(int, lily_value *);

#endif