#include <stdlib.h>

#include "lily.h"

/* This file is to be compiled only by emscripten, and serves as a bridge
   between the js of a browser and Lily's C code.
   Since emscripten can't run main() twice, this instead supplies a series of
   helper functions to be called by the js side. It also has the impl for puts
   that's necessary. */

typedef struct {
    lily_state *state;
    lily_config config;
} sandbox;

void import_noop(lily_state *s, const char *root, const char *source,
        const char *name) {  }

sandbox *get_parser()
{
    sandbox *box = malloc(sizeof(*box));

    lily_config_init(&box->config);
    box->config.import_func = import_noop;

    box->state = lily_new_state(&box->config);

    return box;
}

int run_parser(sandbox *box, char *to_parse)
{
    return lily_parse_string(box->state, "[tryit]", to_parse);
}

void destroy_parser(sandbox *box)
{
    lily_free_state(box->state);
    free(box);
}

char *get_parser_error(sandbox *box)
{
    return lily_error_message(box->state);
}
