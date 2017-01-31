#ifndef LILY_API_EMBED_H
# define LILY_API_EMBED_H

# ifndef LILY_STATE
#  define LILY_STATE
typedef struct lily_vm_state_ lily_state;
# endif

void lily_free_state(lily_state *);
lily_state *lily_new_state(void);

const char *lily_get_error(lily_state *);
const char *lily_get_error_message(lily_state *);

typedef void (*lily_render_func)(const char *, void *);

void lily_op_argv(lily_state *, int, char **);
void lily_op_data(lily_state *, void *);
void lily_op_gc_start(lily_state *, int);
void lily_op_gc_multiplier(lily_state *, int);
void lily_op_render_func(lily_state *, lily_render_func);

char **lily_op_get_argv(lily_state *, int *);
void *lily_op_get_data(lily_state *);
int lily_op_get_gc_start(lily_state *);
int lily_op_get_gc_multiplier(lily_state *);
lily_render_func lily_op_get_render_func(lily_state *);

int lily_parse_string(lily_state *, const char *, const char *);
int lily_parse_file(lily_state *, const char *);
int lily_parse_expr(lily_state *, const char *, char *, const char **);

int lily_render_string(lily_state *, const char *, const char *);
int lily_render_file(lily_state *, const char *);

/* This searches in the scope of the first file loaded, and attempts to find a
   global function based on the name given. Returns either a valid, callable
   function value or NULL. */
struct lily_function_val_ *lily_get_func(lily_state *, const char *);

#endif
