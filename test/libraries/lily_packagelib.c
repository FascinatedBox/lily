/**
library packagelib

This library is used to test that Lily can load a package-pathed library.
*/

#include "lily.h"
#include "lily_packagelib_bindings.h"

/**
define make_list[A](value: A): List[A]

Make a `List` containing the value given.
*/
void lily_packagelib__make_list(lily_state *s)
{
    lily_value *v = lily_arg_value(s, 0);
    lily_container_val *list_val = lily_push_list(s, 1);
    lily_push_value(s, v);
    lily_con_set_from_stack(s, list_val, 0);
    lily_return_top(s);
}

LILY_DECLARE_PACKAGELIB_CALL_TABLE
