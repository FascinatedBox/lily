#ifndef LILY_API_EMBED_H
# define LILY_API_EMBED_H

# include "lily_api_options.h"

# ifndef LILY_STATE
#  define LILY_STATE
typedef struct lily_vm_state_ lily_state;
# endif

void lily_free_state(lily_state *);
lily_state *lily_new_state(lily_options *);

const char *lily_get_error(lily_state *);
void *lily_get_data(lily_state *);

int lily_parse_string(lily_state *, const char *, char *);
int lily_parse_file(lily_state *, const char *);
int lily_parse_expr(lily_state *, const char *, char *, const char **);

int lily_exec_template_string(lily_state *, const char *, char *);
int lily_exec_template_file(lily_state *, const char *);

/* This searches in the scope of the first file loaded, and attempts to find a
   global function based on the name given. Returns either a valid, callable
   function value or NULL. */
struct lily_function_val_ *lily_get_func(lily_state *, const char *);

void lily_register_package(lily_state *, const char *, const char **, void *);

#endif
