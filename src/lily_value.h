#ifndef LILY_VALUE_H
# define LILY_VALUE_H

# include "lily_syminfo.h"

lily_any_val *lily_try_new_any_val();
lily_hash_val *lily_try_new_hash_val();
lily_hash_elem *lily_try_new_hash_elem();
lily_list_val *lily_try_new_list_val();
lily_instance_val *lily_try_new_instance_val();
lily_function_val *lily_try_new_native_function_val(char *);
lily_function_val *lily_try_new_foreign_function_val(lily_foreign_func, char *,
		char *);

void lily_deref_function_val(lily_function_val *);
void lily_deref_string_val(lily_string_val *);
void lily_deref_any_val(lily_any_val *);
void lily_deref_list_val(lily_sig *, lily_list_val *);
void lily_deref_hash_val(lily_sig *, lily_hash_val *);
void lily_deref_tuple_val(lily_sig *, lily_list_val *);
void lily_deref_instance_val(lily_sig *, lily_instance_val *);
void lily_deref_unknown_val(lily_value *);
void lily_deref_unknown_raw_val(lily_sig *, lily_raw_value);

#endif