#ifndef LILY_CLS_HASH_H
#define LILY_CLS_HASH_H

# include "lily_core_types.h"

lily_hash_val *lily_new_hash_val();
lily_hash_elem *lily_new_hash_elem();
int lily_hash_eq(struct lily_vm_state_ *, int *, lily_value *, lily_value *);
void lily_gc_hash_marker(int, lily_value *);
void lily_destroy_hash(lily_value *);
void lily_gc_collect_hash(lily_type *, lily_hash_val *);
lily_class *lily_hash_init(lily_symtab *);

#endif
