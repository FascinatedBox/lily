/**
library covlib

This library exercises less-used parts of the interpreter to help increase
coverage.
*/

#include "lily.h"
#include "lily_int_code_iter.h"

/** Begin autogen section. **/
#define ID_Container(state) lily_cid_at(state, 0)

const char *lily_covlib_info_table[] = {
    "\03Container\0FlatEnum\0ScopedEnum\0"
    ,"N\02Container\0"
    ,"m\0<new>\0(String): Container"
    ,"1\0value\0String"
    ,"E\0FlatEnum\0"
    ,"V\0FlatOne\0"
    ,"V\0FlatTwo\0"
    ,"V\0FlatThree\0"
    ,"E\03ScopedEnum\0"
    ,"V\0ScopedOne\0"
    ,"V\0ScopedTwo\0"
    ,"V\0ScopedThree\0"
    ,"F\0isa_integer\0[A](A): Boolean"
    ,"F\0cover_list_reserve\0"
    ,"F\0cover_func_check\0(Function(Integer),Function(Integer=>String)): Boolean"
    ,"F\0cover_list_sfs\0"
    ,"F\0cover_id_checks\0[A](Coroutine[Integer,Integer],Unit,A,String): Boolean"
    ,"F\0cover_value_as\0(Byte,ByteString,Exception,Coroutine[Integer,Integer],Double,File,Function(Integer),Hash[Integer,Integer],Integer,String)"
    ,"F\0cover_ci_from_native\0(Function(Integer))"
    ,"Z"
};
#define Container_OFFSET 1
#define FlatEnum_OFFSET 4
#define ScopedEnum_OFFSET 8
#define toplevel_OFFSET 12
void lily_covlib_Container_new(lily_state *);
void lily_covlib__isa_integer(lily_state *);
void lily_covlib__cover_list_reserve(lily_state *);
void lily_covlib__cover_func_check(lily_state *);
void lily_covlib__cover_list_sfs(lily_state *);
void lily_covlib__cover_id_checks(lily_state *);
void lily_covlib__cover_value_as(lily_state *);
void lily_covlib__cover_ci_from_native(lily_state *);
lily_call_entry_func lily_covlib_call_table[] = {
    NULL,
    NULL,
    lily_covlib_Container_new,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    lily_covlib__isa_integer,
    lily_covlib__cover_list_reserve,
    lily_covlib__cover_func_check,
    lily_covlib__cover_list_sfs,
    lily_covlib__cover_id_checks,
    lily_covlib__cover_value_as,
    lily_covlib__cover_ci_from_native,
};
/** End autogen section. **/

/**
define isa_integer[A](value: A): Boolean

Return True if 'value' is an `Integer`, False otherwise.
*/
void lily_covlib__isa_integer(lily_state *s)
{
    lily_return_boolean(s, lily_arg_isa(s, 0, LILY_ID_INTEGER));
}

/**
define cover_list_reserve

Extra tests for api function lily_list_reserve.
*/
void lily_covlib__cover_list_reserve(lily_state *s)
{
    lily_container_val *l = lily_push_list(s, 0);
    int i;

    /* Test reserve with a large growth. */
    lily_list_reserve(l, 32);

    /* Test with an unnecessary growth. */
    lily_list_reserve(l, 8);

    /* Push more elements than requested to force another growth. */
    for (i = 0;i < 50;i++) {
        lily_push_integer(s, i);
        lily_value *top = lily_stack_get_top(s);
        lily_list_push(l, top);
        lily_stack_drop_top(s);
    }

    /* Useless zero growth. */
    lily_list_reserve(l, 0);

    lily_stack_drop_top(s);
    lily_return_unit(s);
}

/**
define cover_func_check(native: Function(Integer), foreign: Function(Integer => String)): Boolean

Cover calls to fetch raw parts of certain values.
*/
void lily_covlib__cover_func_check(lily_state *s)
{
    lily_function_val *native_function = lily_arg_function(s, 0);
    lily_function_val *foreign_function = lily_arg_function(s, 1);
    int ok = 1;

    if (lily_function_is_native(native_function) == 0)
        ok = 0;

    if (lily_function_is_native(foreign_function) == 1)
        ok = 0;

    if (lily_function_is_foreign(native_function) == 1)
        ok = 0;

    if (lily_function_is_foreign(foreign_function) == 0)
        ok = 0;

    lily_return_boolean(s, ok);
}

/**
define cover_list_sfs

Cover lily_con_set_from_stack having a target to deref.
*/
void lily_covlib__cover_list_sfs(lily_state *s)
{
    /* Stack: +1 List of nothing. */
    lily_container_val *con_val = lily_push_list(s, 0);

    /* Stack: +1 List of nothing, +1 String. */
    lily_push_string(s, "abc");
    lily_value *top = lily_stack_get_top(s);

    /* Stack: +1 List of "abc", +1 String. */
    lily_list_push(con_val, top);

    /* Stack: +1 List of "abc". */
    lily_stack_drop_top(s);

    /* Stack: +1 List of "abc", +1 String. */
    lily_push_string(s, "def");

    /* Stack: +1 List of "def". */
    lily_con_set_from_stack(s, con_val, 0);

    /* Stack: +0. */
    lily_stack_drop_top(s);

    lily_return_unit(s);
}

/**
define cover_id_checks[A](co: Coroutine[Integer, Integer], u: Unit, c: A, d: String): Boolean

Cover uncommon parts of lily_arg_isa. The last arg should be written as
Container, but parsekit doesn't understand forward class references.
*/
void lily_covlib__cover_id_checks(lily_state *s)
{
    int ok = 1;

    if (lily_arg_isa(s, 0, LILY_ID_COROUTINE) == 0)
        ok = 0;

    if (lily_arg_isa(s, 1, LILY_ID_UNIT) == 0)
        ok = 0;

    if (lily_arg_isa(s, 2, ID_Container(s)) == 0)
        ok = 0;

    if (lily_arg_isa(s, 3, LILY_ID_STRING) == 0)
        ok = 0;

    /* This needs to be covered too. Might as well do it here. */
    (void)lily_arg_generic(s, 3);

    lily_return_boolean(s, ok);
}

/**
define cover_value_as(a: Byte,
                      b: ByteString,
                      c: Exception,
                      d: Coroutine[Integer, Integer],
                      e: Double,
                      f: File,
                      g: Function(Integer),
                      h: Hash[Integer, Integer],
                      i: Integer,
                      j: String)

lily_as_* functions extract the contents of a value. Since these functions are
extremely basic (they just pop the lid off and pull out the insides), no
testing is done on the actual contents. That's covered by the type system.
*/
void lily_covlib__cover_value_as(lily_state *s)
{
    (void)lily_as_byte      (lily_arg_value(s, 0));
    (void)lily_as_bytestring(lily_arg_value(s, 1));
    (void)lily_as_container (lily_arg_value(s, 2));
    (void)lily_as_coroutine (lily_arg_value(s, 3));
    (void)lily_as_double    (lily_arg_value(s, 4));
    (void)lily_as_function  (lily_arg_value(s, 5));
    (void)lily_as_generic   (lily_arg_value(s, 6));
    (void)lily_as_hash      (lily_arg_value(s, 7));
    (void)lily_as_integer   (lily_arg_value(s, 8));
    (void)lily_as_string    (lily_arg_value(s, 9));
    lily_return_unit(s);
}

/**
define cover_ci_from_native(fn: Function(Integer))

This calls lily_ci_from_native, a function provided so that disassemble can
walk through opcodes.
*/
void lily_covlib__cover_ci_from_native(lily_state *s)
{
    lily_function_val *func_val = lily_arg_function(s, 0);
    lily_code_iter ci;

    lily_ci_from_native(&ci, func_val);
    lily_return_unit(s);
}

void lily_covlib_Container_new(lily_state *s)
{
    lily_container_val *con = lily_push_super(s, ID_Container(s), 1);
    lily_con_set(con, 0, lily_arg_value(s, 0));
    lily_return_top(s);
}

/**
native class Container(value: String) {
    private var @value: String
}

Example container for identity testing.
*/


/**
enum FlatEnum {
    FlatOne,
    FlatTwo,
    FlatThree
}

Flat enum test.
*/

/**
scoped enum ScopedEnum {
    ScopedOne,
    ScopedTwo,
    ScopedThree
}

Scoped enum test.
*/
