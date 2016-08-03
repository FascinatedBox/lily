#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "lily_api_options.h"
#include "lily_api_embed.h"
#include "lily_api_value.h"

const char *to_process =
"define fib(n: Integer): Integer\n"
"{\n"
"    if n < 2:\n"
"        return n\n"
"    else:\n"
"        return fib(n - 1) + fib(n - 2)\n"
"}";

int main(int argc, char **argv)
{
    lily_options *options = lily_new_default_options();
    lily_state *state = lily_new_state(options);

    /* The second argument serves as a name, in case an error is to be shown.
       It is important that names be enclosed in brackets. */
    lily_parse_file(state, "fib.lly");

    /* Get fib from the current scope. */
    lily_function_val *fib = lily_get_func(state, "fib");
    int i;

    for (i = 1; i < 10;i++) {
        /* The interpreter's state is established and ready to call fib. */
        lily_push_integer(state, i);

        /* Call fib, sending in 1 argument. The calling stack will consume 1
           argument, thus putting it back as it was before. */
        lily_exec_simple(state, fib, 1);

        printf("fib(%d) is %" PRId64 ".\n", i, lily_result_integer(state));
    }

    lily_free_state(state);
    exit(EXIT_SUCCESS);
}
