#ifndef LILY_VALUE_H
# define LILY_VALUE_H

# include "lily_syminfo.h"

lily_method_val *lily_try_new_method_val();
lily_object_val *lily_try_new_object_val();
lily_hash_val *lily_try_new_hash_val();
lily_hash_elem *lily_try_new_hash_elem();

void lily_deref_method_val(lily_method_val *);
void lily_deref_str_val(lily_str_val *);
void lily_deref_object_val(lily_object_val *);
void lily_deref_list_val_by(lily_sig *, lily_list_val *, int);
void lily_deref_list_val(lily_sig *, lily_list_val *);
void lily_deref_hash_val(lily_sig *, lily_hash_val *);
void lily_deref_unknown_val(lily_value *);
void lily_deref_unknown_raw_val(lily_sig *, lily_raw_value);

#endif