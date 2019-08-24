/**
library covlib

This library exercises less-used parts of the interpreter to help increase
coverage.
*/

#include "lily.h"
#include "lily_int_code_iter.h"
#include "lily_covlib_bindings.h"

LILY_COVLIB_EXPORT
lily_call_entry_func lily_covlib_call_table[];

/**
foreign class Foreign() {
    layout {
    }
}

Example foreign value.
*/
void destroy_Foreign(lily_covlib_Foreign *f) {}
void lily_covlib_Foreign_new(lily_state *s)
{
    INIT_Foreign(s);
    lily_return_top(s);
}

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
define cover_value_group(a: Boolean,
                         b: Byte,
                         c: ByteString,
                         d: Coroutine[Integer, Integer],
                         e: Double,
                         f: Option[Integer],
                         g: File,
                         h: Function(Integer),
                         i: Hash[Integer, Integer],
                         j: Foreign,
                         k: Exception,
                         l: Integer,
                         m: List[Integer],
                         n: String,
                         o: Tuple[Integer],
                         p: Unit,
                         q: Option[Integer]): Boolean

This covers calling lily_value_get_group with every kind of value possible.
*/
void lily_covlib__cover_value_group(lily_state *s)
{
    lily_value_group expect[] = {
        lily_isa_boolean,
        lily_isa_byte,
        lily_isa_bytestring,
        lily_isa_coroutine,
        lily_isa_double,
        lily_isa_empty_variant,
        lily_isa_file,
        lily_isa_function,
        lily_isa_hash,
        lily_isa_foreign_class,
        lily_isa_native_class,
        lily_isa_integer,
        lily_isa_list,
        lily_isa_string,
        lily_isa_tuple,
        lily_isa_unit,
        lily_isa_variant,
    };
    int count = lily_arg_count(s);
    int result = 1;
    int i;

    for (i = 0;i < count;i++) {
        lily_value *v = lily_arg_value(s, i);
        lily_value_group given = lily_value_get_group(v);
        lily_value_group got = expect[i];

        if (given != got) {
            result = 0;
            break;
        }
    }

    lily_return_boolean(s, result);
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

static void ignore_render(const char *to_render, void *data)
{
    (void)data;
    (void)to_render;
}

static void misc_import_hook(lily_state *s, const char *target)
{
    lily_import_use_local_dir(s, "");
    lily_import_string(s, "asdf.lily", "var v = 10");
}

static void misc_dup_import_hook(lily_state *s, const char *target)
{
    lily_import_use_local_dir(s, "");
    lily_import_string(s, "asdf.lily", "var v = 10");
    lily_import_string(s, "asdf.lily", "var v = 10");
    lily_import_library(s, "asdf.xyz");
    lily_import_library_data(s, "asdf.xyz", lily_covlib_info_table, lily_covlib_call_table);
}

static void misc_ldata_import_hook(lily_state *s, const char *target)
{
    lily_import_use_local_dir(s, "");
    lily_import_library_data(s, "asdf.xyz", lily_covlib_info_table, lily_covlib_call_table);
}

static void misc_no_use_import_hook(lily_state *s, const char *target)
{
    lily_import_file(s, "asdf");
    lily_import_library(s, "asdf");
    lily_import_string(s, target, "1");
    /* lily_import_library_data isn't tested because that falls under the rule
       of not passing NULL's to api functions. */
}

/**
define cover_misc_api

Cover a lot of miscellaneous api functions.
*/
void lily_covlib__cover_misc_api(lily_state *s)
{
    lily_config config;
    lily_config_init(&config);
    config.render_func = ignore_render;

    {
        /* The repl likes this because it doesn't have to probe where traceback
           starts. */
        lily_state *subinterp = lily_new_state(&config);
        lily_load_string(subinterp, "[testing]", "?");
        lily_parse_content(subinterp);
        const char *output = lily_error_message_no_trace(subinterp);
        (void)output;
        lily_free_state(subinterp);
    }
    {
        /* Search for function that doesn't exist. */
        lily_state *subinterp = lily_new_state(&config);
        lily_load_string(subinterp, "[subinterp]", "10");
        lily_parse_content(subinterp);
        lily_function_val *v = lily_find_function(subinterp, "asdf");
        (void)v;
        lily_free_state(subinterp);
    }
    {
        /* Filename must end with .lily. */
        lily_state *subinterp = lily_new_state(&config);
        lily_load_file(subinterp, "asdf.xyz");
        lily_free_state(subinterp);
    }
    {
        /* Fail to parse so rewind is activated. */
        lily_state *subinterp = lily_new_state(&config);
        lily_load_file(subinterp, "verify_double.lily");
        lily_parse_content(subinterp);
        lily_load_file(subinterp, "verify_double.lily");
        lily_parse_content(subinterp);
        lily_free_state(subinterp);
    }
    {
        /* Import failure. Library suffixes are different depending on the
           platform, and Lily can't handle that yet. */
        lily_state *subinterp = lily_new_state(&config);
        lily_load_string(subinterp, "[testing]", "import missing");
        lily_parse_content(subinterp);
        lily_free_state(subinterp);
    }
    {
        /* Import a library within a package. */
        lily_state *subinterp = lily_new_state(&config);
        lily_load_string(subinterp, "test/[testing]", "import packagelib\n"
                                                       "packagelib.make_list(1)");
        lily_parse_content(subinterp);
        lily_free_state(subinterp);
    }
    {
        /* Fail to import a library. */
        lily_state *subinterp = lily_new_state(&config);
        lily_load_string(subinterp, "test/[testing]", "import brokenlib");
        lily_parse_content(subinterp);
        lily_free_state(subinterp);
    }
    {
        /* Config to copy parsed strings. */
        lily_state *subinterp = lily_new_state(&config);
        config.import_func = misc_import_hook;
        lily_load_string(subinterp, "[testing]", "import asdf");
        lily_parse_content(subinterp);
        lily_free_state(subinterp);
    }
    {
        /* Block multiple imports at once. */
        lily_state *subinterp = lily_new_state(&config);
        config.import_func = misc_dup_import_hook;
        lily_load_string(subinterp, "[testing]", "import asdf");
        lily_parse_content(subinterp);
        lily_free_state(subinterp);
    }
    {
        /* Load our library data. */
        lily_state *subinterp = lily_new_state(&config);
        config.import_func = misc_ldata_import_hook;
        lily_load_string(subinterp, "[testing]", "import asdf");
        lily_parse_content(subinterp);
        lily_free_state(subinterp);
    }
    {
        /* Import hook that doesn't set a 'use' first. Output isn't checked, as
           this is for ensuring that doesn't crash. */
        lily_state *subinterp = lily_new_state(&config);
        config.import_func = misc_no_use_import_hook;
        lily_load_string(subinterp, "[testing]", "import asdf");
        lily_parse_content(subinterp);
        lily_free_state(subinterp);
    }
    {
        /* Validate code without running it. */
        lily_state *subinterp = lily_new_state(&config);
        lily_load_string(subinterp, "[testing]", "print(\"Not validating code properly.\"");
        lily_validate_content(subinterp);
        lily_free_state(subinterp);
    }
}

/**
native class Container(value: String) {
    private var @value: String
}

Example container for testing.
*/

void lily_covlib_Container_new(lily_state *s)
{
    lily_container_val *con = SUPER_Container(s);
    SET_Container__value(con, lily_arg_value(s, 0));
    lily_return_super(s);
}

/**
define Container.update(x: String)

Set value in container.
*/
void lily_covlib_Container_update(lily_state *s)
{
    lily_container_val *con = lily_arg_container(s, 0);
    SET_Container__value(con, lily_arg_value(s, 1));
    lily_return_unit(s);
}

/**
define Container.fetch: String

Get value inside container.
*/
void lily_covlib_Container_fetch(lily_state *s)
{
    lily_container_val *con = lily_arg_container(s, 0);
    lily_return_value(s, GET_Container__value(con));
}

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

LILY_DECLARE_COVLIB_CALL_TABLE
