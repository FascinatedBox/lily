#ifndef LILY_CLS_FILE_H
#define LILY_CLS_FILE_H

# include "lily_core_types.h"

lily_file_val *lily_new_file_val(FILE *, char);
void lily_destroy_file(lily_value *);
int lily_file_setup(lily_symtab *, lily_class *);

#endif
