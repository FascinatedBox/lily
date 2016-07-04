#include "lily_api_options.h"

#include "lily_parser.h"

/* This file is to be compiled only by emscripten, and serves as a bridge
   between the js of a browser and Lily's C code.
   Since emscripten can't run main() twice, this instead supplies a series of
   helper functions to be called by the js side. It also has the impl for puts
   that's necessary. */

lily_parse_state *get_parser()
{
    lily_options *options = lily_new_default_options();
    lily_parse_state *parser = lily_new_parse_state(options);
    options->allow_sys = 0;

    lily_free_options(options);
    return parser;
}

int run_parser(lily_parse_state *parser, char *to_parse)
{
    return lily_parse_string(parser, "[tryit]", lm_no_tags, to_parse);
}

void destroy_parser(lily_parse_state *parser)
{
    lily_free_parse_state(parser);
}

char *get_parser_error(lily_parse_state *parser)
{
    return lily_build_error_message(parser);
}
