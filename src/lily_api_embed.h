#ifndef LILY_API_EMBED_H
# define LILY_API_EMBED_H

# ifndef LILY_STATE
#  define LILY_STATE
typedef struct lily_vm_state_ lily_state;
# endif

typedef void (*lily_render_func)(const char *, void *);
typedef void (*lily_import_func)(lily_state *, const char *, const char *,
        const char *);

typedef struct lily_config_ {
    int argc;
    char **argv;

    int gc_multiplier;
    int gc_start;

    lily_render_func render_func;
    lily_import_func import_func;

    void *data;
} lily_config;

void lily_free_state(lily_state *);
void lily_init_config(lily_config *);
lily_state *lily_new_state(lily_config *);

const char *lily_get_error(lily_state *);
const char *lily_get_error_message(lily_state *);

int lily_open_file(lily_state *, const char *);
int lily_open_string(lily_state *, const char *, const char *);
int lily_open_library(lily_state *, const char *);
int lily_open_library_data(lily_state *, const char *, const char **, void *);

int lily_parse_string(lily_state *, const char *, const char *);
int lily_parse_file(lily_state *, const char *);
int lily_parse_expr(lily_state *, const char *, char *, const char **);

int lily_render_string(lily_state *, const char *, const char *);
int lily_render_file(lily_state *, const char *);

lily_config *lily_get_config(lily_state *);

/* This searches in the scope of the first file loaded, and attempts to find a
   global function based on the name given. Returns either a valid, callable
   function value or NULL. */
struct lily_function_val_ *lily_get_func(lily_state *, const char *);

/* Registering a C package makes it available for the interpreter to load when
   'import <name>' is used. */

#define LILY_DECLARE_PACKAGE(name) \
extern const char **lily_##name##_table; \
void *lily_##name##_loader(lily_state *s, int)

#define LILY_REGISTER_PACKAGE(state, name) \
lily_register_package(state, #name, lily_##name##_table, lily_##name##_loader)

/* Should only be called through the macro. */
void lily_register_package(lily_state *, const char *, const char **, void *);

#endif
