#ifndef LILY_LIBRARY_H
# define LILY_LIBRARY_H

# include "lily_core_types.h"

lily_library *lily_library_load(const char *);
void lily_library_free(void *);

#endif
