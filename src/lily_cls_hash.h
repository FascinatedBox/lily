#ifndef LILY_CLS_HASH_H
#define LILY_CLS_HASH_H

# include "lily_core_types.h"

lily_hash_val *lily_new_hash_val();

lily_hash_elem *lily_hash_get_elem(lily_vm_state *vm, lily_hash_val *,
        lily_value *);
void lily_hash_set_elem(lily_vm_state *, lily_hash_val *, lily_value *,
        lily_value *);
void lily_hash_add_unique(lily_vm_state *, lily_hash_val *, lily_value *,
        lily_value *);

void lily_gc_hash_marker(int, lily_value *);
void lily_destroy_hash(lily_value *);
lily_class *lily_hash_init(lily_symtab *);

#endif
