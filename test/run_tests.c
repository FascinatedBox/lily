#include <stdlib.h>
#include <stdio.h>

#include "lily.h"

extern const char *lily_extend_info_table[];
extern lily_call_entry_func lily_extend_call_table[];

int main(int argc, char **argv)
{
    lily_config config;

    lily_config_init(&config);

    lily_state *state = lily_new_state(&config);
    lily_module_register(state, "extend", lily_extend_info_table,
            lily_extend_call_table);

#ifdef _WIN32
    lily_load_file(state, "test\\test_main.lily");
#else
    lily_load_file(state, "test/test_main.lily");
#endif

    if (lily_parse_content(state) == 0) {
        fputs(lily_error_message(state), stderr);
        exit(EXIT_FAILURE);
    }

    lily_function_val *f = lily_find_function(state, "did_pass");
    lily_call_prepare(state, f);
    lily_call(state, 0);

    int code = EXIT_SUCCESS;

    if (lily_as_boolean(lily_call_result(state)) == 0)
        code = EXIT_FAILURE;

    lily_free_state(state);
    exit(code);
}
