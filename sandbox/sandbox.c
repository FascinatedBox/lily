#include "lily_api_embed.h"

/* This file is to be compiled only by emscripten, and serves as a bridge
   between the js of a browser and Lily's C code.
   Since emscripten can't run main() twice, this instead supplies a series of
   helper functions to be called by the js side. It also has the impl for puts
   that's necessary. */

lily_state *get_parser()
{
    lily_options *options = lily_new_options();
    lily_state *state = lily_new_state(options);
    lily_op_allow_sys(options, 1);

    return state;
}

int run_parser(lily_state *state, char *to_parse)
{
    return lily_parse_string(state, "[tryit]", to_parse);
}

void destroy_parser(lily_state *state)
{
    lily_free_state(state);
}

char *get_parser_error(lily_state *state)
{
    return lily_get_error(state);
}
