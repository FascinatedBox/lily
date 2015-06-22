#ifndef LILY_CLS_FILE_H
#define LILY_CLS_FILE_H

# include "lily_core_types.h"

lily_file_val *lily_new_file_val(FILE *, char);
void lily_destroy_file(lily_value *);
lily_class *lily_file_init(lily_symtab *);

#endif
