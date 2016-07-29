#ifndef LILY_API_EMBED_H
# define LILY_API_EMBED_H

# include "lily_api_options.h"

typedef struct lily_parse_state_ lily_state;

void lily_free_state(lily_state *);
lily_state *lily_new_state(lily_options *);

const char *lily_get_error(lily_state *);

int lily_parse_string(lily_state *, const char *, char *);
int lily_parse_file(lily_state *, const char *);

int lily_exec_template_string(lily_state *, const char *, char *);
int lily_exec_template_file(lily_state *, const char *);

void lily_register_package(lily_state *, const char *, const char **, void *);

#endif
