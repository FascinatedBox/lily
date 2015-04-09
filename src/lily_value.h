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
lily_file_val *lily_new_file_val(lily_mem_func, FILE *, char);
lily_instance_val *lily_new_instance_val(lily_mem_func);
lily_function_val *lily_new_native_function_val(lily_mem_func,
        char *, char *);
lily_function_val *lily_new_foreign_function_val(lily_mem_func,
        lily_foreign_func, char *, char *);

void lily_deref(lily_mem_func, lily_value *);
void lily_deref_raw(lily_mem_func, lily_type *, lily_raw_value);

void lily_destroy_function(lily_mem_func, lily_value *);
void lily_destroy_string(lily_mem_func, lily_value *);
void lily_destroy_symbol(lily_mem_func, lily_value *);
void lily_destroy_any(lily_mem_func, lily_value *);
void lily_destroy_list(lily_mem_func, lily_value *);
void lily_destroy_hash(lily_mem_func, lily_value *);
void lily_destroy_tuple(lily_mem_func, lily_value *);
void lily_destroy_file(lily_mem_func, lily_value *);
void lily_destroy_instance(lily_mem_func, lily_value *);

#endif