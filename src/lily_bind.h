#ifndef LILY_BIND_H
# define LILY_BIND_H

# include "lily_core_types.h"
# include "lily_symtab.h"

lily_value *lily_bind_string(lily_symtab *, const char *);
lily_value *lily_bind_integer(lily_symtab *, int64_t);
lily_value *lily_bind_string_take_buffer(lily_symtab *, char *);

#endif
