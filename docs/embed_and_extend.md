Embedding and Extending Lily
============================

This document provides examples for embedding and extending the interpreter.
Before you begin, you'll need to grab the `FascinatedBox/lily-parsekit` repo so
that you have the `bindgen.lily` script that creates bindings. You may also want
to read through the documentation there on extending/embedding Lily. You'll also
need to `make install` the interpreter so you have the `lily.h` header.

### C to fib

This creates a fibonacci function in Lily, and calls it repeatedly from C.

```
#include <stdlib.h>
#include <inttypes.h>

#include "lily.h"

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
    lily_config config;

    lily_config_init(&config);

    lily_state *state = lily_new_state(&config);

    /* The second argument is the name, and must be in brackets.
       When the parse is done, any code written outside of a function definition
       is executed. */
    lily_parse_string(state, "[test]", to_process);
    lily_function_val *fib = lily_find_function(state, "fib");
    lily_call_prepare(state, fib);

    lily_value *result = lily_call_result(state);
    int i;

    for (i = 1; i < 10;i++) {
        lily_push_integer(state, i);
        lily_call(state, 1);

        printf("fib(%d) is %" PRId64 ".\n", i, lily_as_integer(result));
    }

    lily_free_state(state);
    exit(EXIT_SUCCESS);
}
```

### Template call

This example provides argv to a template, so that the template can later print
the arguments passed.

```
#include <stdio.h>
#include <stdlib.h>

#include "lily.h"

/* A Lily tag is required at the start of input. This allows the interpreter to
   prevent accidentally sending code-only content. */
const char *template =
"<?lily ?>\n"
"<html>\n"
"   <body>\n"
"       <?lily\n"
"           import sys\n"
"           print(sys.argv)\n"
"       ?>\n"
"   </body>\n"
"</html>";

int main(int argc, char **argv)
{
    lily_config config;

    lily_config_init(&config);
    config.data = stdout;
    config.render_func = (lily_render_func) fputs;
    config.argc = argc;
    config.argv = argv;

    lily_state *state = lily_new_state(&config);
    int result = lily_render_string(state, "[example]", template);

    /* There isn't an error, but if there was one, this would print it. */
    if (result == 0)
        fputs(lily_error_message(state), stderr);

    lily_free_state(state);
    exit(EXIT_SUCCESS);
}
```

### Register and embed

This example starts an interpreter and makes a package named `hello` available
to it. Registering a module is only necessary if you're embedding an
interpreter.

Even when a module is registered, the calling script must still explicitly
import it. This is done intentionally, as it prevents the script from assuming
any registered modules. Such a design allows transparent replacement of modules,
so long as the provide the same interface.

```
#include <stdio.h>
#include <stdlib.h>

#include "lily.h"

/**
library hello

This is an example of how to create a module and provide it to Lily.
*/

/** Begin autogen section. **/
/** End autogen section. **/

/**
define greet(name: String)

This writes `Hello, $name` to stdout.
*/
void lily_hello__greet(lily_state *state)*
{
    lily_msgbuf *msgbuf = lily_msgbuf_get(state);
    const char *name = lily_arg_string_raw(state, 0);

    /* This uses the same msgbuf trick as the last example. The text is valid
       until the next msgbuf usage. */
    const char *text = lily_mb_sprintf(msgbuf, "Hello, %s!\n", name);

    fputs(text, stdout);

    lily_return_unit(state);
}

/**
define add(a: Integer, b: Integer): Integer

Add two numbers together and return the result.
*/
void lily_hello__add(lily_state *state)
{
    int64_t left = lily_arg_integer(state, 0);
    int64_t right = lily_arg_integer(state, 1);

    int64_t result = left + right;

    lily_return_integer(state, result);
}

const char *script =
"import hello\n"
"\n"
"hello.greet(\"World\")\n"
"print(\"2 + 2 is {0}\".format(hello.add(2, 2)))\n";

int main(int argc, char **argv)
{
    lily_config config;

    lily_config_init(&config);

    lily_state *state = lily_new_state(&config);

    lily_module_register(state, "hello", lily_hello_table, lily_hello_loader);
    lily_parse_string(state, "[hello]", script);
    lily_free_state(state);

    exit(EXIT_SUCCESS);
}
```

### Create a Lily library

This example creates a library that Lily can load. Binding is done just like
before:

`lily bindgen.lily lily_box.c`

You can test the box library by loading and using it:

`lily -s 'import box print(box.box(10))'`

```
#include "lily.h"

/**
library box

This will build a library for Lily called `box`. When you build this file, make
sure that the resulting output doesn't have a "lib" prefix, or Lily won't load
it.
*/

/** Begin autogen section. **/
/** End autogen section. **/

/**
define box[A](input: A): List[A]

This function returns a `List` holding the value provided.
*/
void lily_box__box(lily_state *state)
{
    lily_container_val *con = lily_push_list(state, 1);
    lily_con_set(con, 0, lily_arg_value(state, 0));
    lily_return_top(state);
}
```
