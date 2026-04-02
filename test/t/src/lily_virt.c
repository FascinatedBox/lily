#include "lily.h"
#include "lily_virt_bindings.h"

void lily_virt_new_VOne(lily_state *s)
{
    lily_container_val *con = SUPER_virt_VOne(s);

    lily_push_integer(s, 100);
    SETFS_virt_VOne__x(s, con);
    lily_return_super(s);
}

void lily_virt_VOne_add_to_var(lily_state *s)
{
    lily_container_val *con = lily_arg_container(s, 0);
    int64_t i = lily_arg_integer(s, 1);
    lily_value *v = GET_virt_VOne__x(con);

    i += lily_as_integer(v);
    lily_push_integer(s, i);
    SETFS_virt_VOne__x(s, con);

    lily_return_unit(s);
}

void lily_virt_VOne_f(lily_state *s)
{
    lily_return_unit(s);
}

void lily_virt_new_VThree(lily_state *s)
{
    lily_container_val *con = SUPER_virt_VThree(s);

    lily_push_integer(s, 100);
    SETFS_virt_VOne__x(s, con);
    lily_push_integer(s, 2000);
    SETFS_virt_VThree__y(s, con);
    lily_return_super(s);
}

void lily_virt_VThree_add_to_var(lily_state *s)
{
    lily_container_val *con = lily_arg_container(s, 0);
    int64_t i = lily_arg_integer(s, 1);
    lily_value *v = GET_virt_VThree__y(con);

    i += lily_as_integer(v);
    lily_push_integer(s, i);
    SETFS_virt_VThree__y(s, con);

    lily_return_unit(s);
}

void lily_virt_VThree_get_name(lily_state *s)
{
    lily_return_string(s, "VThree");
}

void lily_virt_VThree_i(lily_state *s)
{
    lily_return_unit(s);
}

void lily_virt_new_VTwo(lily_state *s)
{
    lily_virt_new_VOne(s);
}

void lily_virt_VTwo_h(lily_state *s)
{
    lily_return_unit(s);
}

LILY_DECLARE_VIRT_CALL_TABLE
