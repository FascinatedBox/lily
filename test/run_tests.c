#include <stdlib.h>
#include <stdio.h>

#include "lily_api_embed.h"
#include "lily_api_value.h"

extern const char *lily_extend_table[];
void *lily_extend_loader(lily_state *s, int);

int main(int argc, char **argv)
{
    lily_config config;

    lily_init_config(&config);

    lily_state *state = lily_new_state(&config);
    LILY_REGISTER_PACKAGE(state, extend);

#ifdef _WIN32
    if (lily_parse_file(state, "test\\test_main.lily") == 0) {
#else
    if (lily_parse_file(state, "test/test_main.lily") == 0) {
#endif
        fputs(lily_get_error(state), stderr);
        exit(EXIT_FAILURE);
    }

    lily_function_val *f = lily_get_func(state, "did_pass");
    lily_call_prepare(state, f);
    lily_call(state, 0);

    int code = EXIT_SUCCESS;

    if (lily_as_boolean(lily_call_result(state)) == 0)
        code = EXIT_FAILURE;

    lily_free_state(state);
    exit(code);
}
