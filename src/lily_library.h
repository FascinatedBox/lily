#ifndef LILY_LIBRARY_H
# define LILY_LIBRARY_H

# include "lily_core_types.h"

lily_library *lily_library_load(char *);
void lily_library_free(lily_library *);

#endif
