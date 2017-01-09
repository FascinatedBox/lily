#ifndef LILY_API_OPTIONS_H
# define LILY_API_OPTIONS_H

typedef void (*lily_render_func)(char *, void *);
typedef struct lily_options_ lily_options;

lily_options *lily_new_options(void);
void lily_free_options(lily_options *);

void lily_op_allow_sys(lily_options *, int);
void lily_op_argv(lily_options *, int, char **);
void lily_op_data(lily_options *, void *);
void lily_op_freeze(lily_options *);
void lily_op_gc_start(lily_options *, int);
void lily_op_gc_multiplier(lily_options *, int);
void lily_op_render_func(lily_options *, lily_render_func);

int lily_op_get_allow_sys(lily_options *);
char **lily_op_get_argv(lily_options *, int *);
void *lily_op_get_data(lily_options *);
int lily_op_get_gc_start(lily_options *);
int lily_op_get_gc_multiplier(lily_options *);
lily_render_func lily_op_get_render_func(lily_options *);

#endif
