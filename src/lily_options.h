#ifndef LILY_OPTIONS_H
# define LILY_OPTIONS_H

# include <inttypes.h>

typedef void (*lily_render_func)(const char *, void *);

typedef struct {
    int argc;
    /* This is what `sys.argv` makes visible. */
    char **argv;
    /* The html sender gets this as the second argument. */
    void *data;
    /* This is called by lexer when content is seen in template mode. */
    lily_render_func render_func;
} lily_options;

lily_options *lily_new_options(void);
void lily_free_options(lily_options *);

#endif
