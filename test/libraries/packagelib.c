/**
library packagelib

This library is used to test that Lily can load a package-pathed library.
*/

#include "lily.h"

/** Begin autogen section. **/
const char *lily_packagelib_info_table[] = {
    "\0\0"
    ,"F\0make_list\0[A](A): List[A]"
    ,"Z"
};
#define toplevel_OFFSET 1
void lily_packagelib__make_list(lily_state *);
lily_call_entry_func lily_packagelib_call_table[] = {
    NULL,
    lily_packagelib__make_list,
};
/** End autogen section. **/

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
