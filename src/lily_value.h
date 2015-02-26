#ifndef LILY_VALUE_H
# define LILY_VALUE_H

# include "lily_core_types.h"

/* There is no lily_new_symbol_val. This is because symbol values must be
   unique, and that uniqueness is enforced by symtab.
   Use lily_symbol_by_name from lily_symtab.h to acquire symbol values. */
lily_any_val *lily_new_any_val(lily_mem_func);
lily_hash_val *lily_new_hash_val(lily_mem_func);
lily_hash_elem *lily_new_hash_elem(lily_mem_func);
lily_list_val *lily_new_list_val(lily_mem_func);
lily_instance_val *lily_new_instance_val(lily_mem_func);
lily_function_val *lily_new_native_function_val(lily_mem_func,
        char *, char *);
lily_function_val *lily_new_foreign_function_val(lily_mem_func,
        lily_foreign_func, char *, char *);

void lily_deref_function_val(lily_mem_func, lily_function_val *);
void lily_deref_string_val(lily_mem_func, lily_string_val *);
void lily_deref_symbol_val(lily_mem_func, lily_symbol_val *);
void lily_deref_any_val(lily_mem_func, lily_any_val *);
void lily_deref_list_val(lily_mem_func, lily_type *, lily_list_val *);
void lily_deref_hash_val(lily_mem_func, lily_type *, lily_hash_val *);
void lily_deref_tuple_val(lily_mem_func, lily_type *, lily_list_val *);
void lily_deref_instance_val(lily_mem_func, lily_type *, lily_instance_val *);
void lily_deref_unknown_val(lily_mem_func, lily_value *);
void lily_deref_unknown_raw_val(lily_mem_func, lily_type *, lily_raw_value);

#endif