#include <string.h>

#include "lily.h"
#include "lily_code_iter.h"
#include "lily_covlib_bindings.h"

LILY_COVLIB_EXPORT
lily_call_entry_func lily_covlib_call_table[];

typedef struct lily_covlib_Foreign_ {
    LILY_FOREIGN_HEADER
} lily_covlib_Foreign;

typedef struct lily_covlib_ForeignGeneric_ {
    LILY_FOREIGN_HEADER
} lily_covlib_ForeignGeneric;

void lily_covlib_destroy_Foreign(lily_covlib_Foreign *f)
{
    (void)f;
}

void lily_covlib_new_Foreign(lily_state *s)
{
    INIT_Foreign(s);
    lily_return_top(s);
}

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

void lily_covlib__cover_id_checks(lily_state *s)
{
    int ok = 1;

    if (lily_arg_isa(s, 0, LILY_ID_UNIT) == 0)
        ok = 0;

    if (lily_arg_isa(s, 1, ID_Container(s)) == 0)
        ok = 0;

    if (lily_arg_isa(s, 2, LILY_ID_STRING) == 0)
        ok = 0;

    /* This needs to be covered too. Might as well do it here. */
    (void)lily_arg_generic(s, 2);

    lily_return_boolean(s, ok);
}

void lily_covlib__cover_push_boolean(lily_state *s)
{
    lily_push_boolean(s, 1);
    lily_return_top(s);
}

void lily_covlib__cover_value_as(lily_state *s)
{
    (void)lily_as_byte      (lily_arg_value(s, 0));
    (void)lily_as_bytestring(lily_arg_value(s, 1));
    (void)lily_as_container (lily_arg_value(s, 2));
    (void)lily_as_double    (lily_arg_value(s, 3));
    (void)lily_as_file      (lily_arg_value(s, 4));
    (void)lily_as_function  (lily_arg_value(s, 5));
    (void)lily_as_generic   (lily_arg_value(s, 6));
    (void)lily_as_hash      (lily_arg_value(s, 7));
    (void)lily_as_integer   (lily_arg_value(s, 8));
    (void)lily_as_string    (lily_arg_value(s, 9));
    lily_return_unit(s);
}

void lily_covlib__cover_optional_double(lily_state *s)
{
    double arg_a = lily_optional_double(s, 0, 100.0);
    double arg_b = lily_optional_double(s, 1, 200.0);
    double arg_c = lily_optional_double(s, 2, 300.0);
    double total = arg_a + arg_b + arg_c;

    lily_return_double(s, total);
}

void lily_covlib__cover_optional_integer(lily_state *s)
{
    int64_t arg_a = lily_optional_integer(s, 0, 100);
    int64_t arg_b = lily_optional_integer(s, 1, 200);
    int64_t arg_c = lily_optional_integer(s, 2, 300);
    int64_t total = arg_a + arg_b + arg_c;

    lily_return_integer(s, total);
}

void lily_covlib__cover_optional_boolean(lily_state *s)
{
    int arg_a = lily_optional_boolean(s, 0, 1);
    int arg_b = lily_optional_boolean(s, 1, 1);
    int arg_c = lily_optional_boolean(s, 2, 1);
    int total = arg_a + arg_b + arg_c;

    lily_return_integer(s, total);
}

void lily_covlib__cover_optional_keyarg_call(lily_state *s)
{
    lily_call_prepare(s, lily_arg_function(s, 0));

    lily_value *result = lily_call_result(s);

    lily_push_unset(s);
    lily_push_integer(s, 1);
    lily_call(s, 2);

    lily_return_value(s, result);
}

void lily_covlib__cover_optional_string(lily_state *s)
{
    const char *arg_a = lily_optional_string_raw(s, 0, "");
    const char *arg_b = lily_optional_string_raw(s, 1, "");
    const char *arg_c = lily_optional_string_raw(s, 2, "");
    lily_msgbuf *msgbuf = lily_msgbuf_get(s);

    lily_mb_add_fmt(msgbuf, "%s%s%s", arg_a, arg_b, arg_c);
    lily_return_string(s, lily_mb_raw(msgbuf));
}

void lily_covlib__cover_value_group(lily_state *s)
{
    lily_value_group expect[] = {
        lily_isa_boolean,
        lily_isa_byte,
        lily_isa_bytestring,
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

void lily_covlib__cover_function_bytecode(lily_state *s)
{
    lily_function_val *foreign_func = lily_arg_function(s, 0);
    lily_function_val *native_func = lily_arg_function(s, 1);
    uint16_t foreign_len, native_len;
    int result = 1;

    uint16_t *foreign_code = lily_function_bytecode(foreign_func, &foreign_len);
    uint16_t *native_code = lily_function_bytecode(native_func, &native_len);

    if (foreign_code != NULL ||
        foreign_len != 0 ||
        native_code == NULL ||
        native_len == 0)
        result = 0;

    lily_return_boolean(s, result);
}

void lily_covlib__make_flat_n(lily_state *s)
{
    int64_t arg_n = lily_arg_integer(s, 0);

    if (arg_n == 1)
        PUSH_FlatOne(s);
    else if (arg_n == 2)
        PUSH_FlatTwo(s);
    else
        PUSH_FlatThree(s);

    lily_return_top(s);
}

void lily_covlib__make_scoped_n(lily_state *s)
{
    int64_t arg_n = lily_arg_integer(s, 0);

    if (arg_n == 1)
        PUSH_ScopedEnum_ScopedOne(s);
    else if (arg_n == 2)
        PUSH_ScopedEnum_ScopedTwo(s);
    else
        PUSH_ScopedEnum_ScopedThree(s);

    lily_return_top(s);
}

void lily_covlib__scoop_narrow(lily_state *s)
{
    lily_return_unit(s);
}

void lily_covlib__scoop_narrow_with_args(lily_state *s)
{
    lily_return_unit(s);
}

static void ignore_render(const char *to_render, void *data)
{
    (void)data;
    (void)to_render;
}

static void misc_dup_import_hook(lily_state *s, const char *target)
{
    (void)target;
    lily_import_use_local_dir(s, "");
    lily_import_string(s, "asdf.lily", "var v = 10");
    lily_import_string(s, "asdf.lily", "var v = 10");
    lily_import_library(s, "asdf.xyz");
    lily_import_library_data(s, "asdf.xyz", lily_covlib_info_table, lily_covlib_call_table);
}

static void misc_ldata_import_hook(lily_state *s, const char *target)
{
    (void)target;
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

static void misc_no_global_import_hook(lily_state *s, const char *target)
{
    if (strcmp(target, "asdf") == 0) {
        lily_import_use_package_dir(s, "");
        lily_import_string(s, "asdf", "import covlib");
    }
}

void lily_covlib__cover_misc_api(lily_state *s)
{
    (void)s;

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
        /* Block multiple imports at once. */
        lily_state *subinterp = lily_new_state(&config);
        config.import_func = misc_dup_import_hook;
        lily_load_string(subinterp, "[test]", "import asdf");
        lily_parse_content(subinterp);
        lily_free_state(subinterp);
    }
    {
        /* Load our library data. */
        lily_state *subinterp = lily_new_state(&config);
        config.import_func = misc_ldata_import_hook;
        lily_load_string(subinterp, "[test]", "import asdf");
        lily_parse_content(subinterp);
        lily_free_state(subinterp);
    }
    {
        /* Import hook that doesn't set a 'use' first. Output isn't checked, as
           this is for ensuring that doesn't crash. */
        lily_state *subinterp = lily_new_state(&config);
        config.import_func = misc_no_use_import_hook;
        lily_load_string(subinterp, "[test]", "import asdf");
        lily_parse_content(subinterp);
        lily_free_state(subinterp);
    }
    {
        /* Validate code without running it. */
        lily_state *subinterp = lily_new_state(&config);
        const char *message;
        lily_load_string(subinterp, "[test]",
                "print(\"Not validating code properly.\")");
        lily_validate_content(subinterp);
        message = lily_error_message_no_trace(subinterp);

        if (message[0] != '\0')
            lily_RuntimeError(s, "Subinterprer verify failed.");

        lily_load_string(subinterp, "[test]", "asdf");
        lily_validate_content(subinterp);
        message = lily_error_message_no_trace(subinterp);

        if (message[0] == '\0')
            lily_RuntimeError(s,
                    "Subinterpreter verify does not show an error.");

        lily_free_state(subinterp);
    }
    {
        lily_config config2;
        lily_config_init(&config2);

        config2.import_func = misc_no_global_import_hook;

        /* Registered modules shouldn't be globally visible. */
        lily_state *subinterp = lily_new_state(&config2);
        lily_module_register(subinterp, "covlib", lily_covlib_info_table,
                lily_covlib_call_table);
        lily_load_string(subinterp, "[test]", "import asdf");
        lily_parse_content(subinterp);
        const char *output = lily_error_message(subinterp);
        (void)output;
        lily_free_state(subinterp);
    }
    {
        lily_config config2;
        lily_config_init(&config2);

        lily_state *subinterp = lily_new_state(&config2);

        lily_load_string(subinterp, "[test]", "<?lily stdout.close()\n?>\n");
        lily_render_content(subinterp);

        const char *message = lily_error_message_no_trace(subinterp);

        if (strcmp(message, "Cannot write template output to stdout.") != 0)
            lily_RuntimeError(s,
                    "Template write when closed failed incorrectly (%s).",
                    message);

        lily_load_string(subinterp, "[test]",
                "<?lily stdout = stdin \n?>\nasdf\n");
        lily_render_content(subinterp);

        message = lily_error_message_no_trace(subinterp);

        if (strcmp(message, "Cannot write template output to stdout.") != 0)
            lily_RuntimeError(s,
                    "Template write when read failed incorrectly (%s).",
                    message);

        lily_free_state(subinterp);

        if (lily_is_valid_utf8("\255\255\255"))
            lily_RuntimeError(s, "utf8 check failed.");
    }
    {
        lily_config config2;
        lily_config_init(&config2);

        char *config_argv[] = {"a"};

        config2.argc = 1;
        config2.argv = config_argv;

        lily_state *subinterp = lily_new_state(&config2);
        lily_load_string(subinterp, "[test]", "import sys\nsys.argv");
        lily_parse_content(subinterp);
        lily_free_state(subinterp);
    }
    {
        lily_msgbuf *msgbuf = lily_msgbuf_get(s);

        lily_mb_add_fmt(msgbuf, "%c", 'a');

        if (strcmp(lily_mb_raw(msgbuf), "a") != 0)
            lily_RuntimeError(s, "Msgbuf percent-c failed.");

        lily_mb_flush(msgbuf);
        lily_mb_add_fmt(msgbuf, "%%qwer%%ty%%");

        if (strcmp(lily_mb_raw(msgbuf), "%qwer%ty%") != 0)
            lily_RuntimeError(s, "Msgbuf percent-percent failed.");

        lily_mb_flush(msgbuf);
        lily_mb_add_fmt(msgbuf, "%");

        if (strcmp(lily_mb_raw(msgbuf), "") != 0)
            lily_RuntimeError(s, "Msgbuf percent at end failed.");

        lily_mb_flush(msgbuf);
        lily_mb_add_fmt(msgbuf, "asdf1234");

        if (lily_mb_pos(msgbuf) != 8)
            lily_RuntimeError(s, "Msgbuf pos failed.");
    }
    {
        lily_config config2;
        lily_config_init(&config2);

        lily_state *subinterp = lily_new_state(&config2);
        int failed = 0;

        /* Running without content should return 0. */
        failed = failed || lily_parse_expr(subinterp, NULL);
        failed = failed || lily_render_content(subinterp);
        failed = failed || lily_validate_content(subinterp);
        failed = failed || lily_parse_content(subinterp);
        failed = failed || lily_parse_manifest(subinterp);

        if (failed)
            lily_RuntimeError(s, "Empty content handling failed.");

        lily_load_file(subinterp, "doesnotexist.lily");

        const char *message = lily_error_message(subinterp);
        const char *expect = "Error: Failed to open doesnotexist.lily: ";

        if (strncmp(message, expect, strlen(expect)) != 0)
            lily_RuntimeError(s,
                    "Wrong error when opening a file that does not exist.");

        lily_load_string(subinterp, "[test]", "import sys\nsys.exit_success()");

        if (lily_load_string(subinterp, "[check]", "2") ||
            lily_load_file(subinterp, "check.lily"))
            lily_RuntimeError(s, "Double loading content.");

        lily_parse_content(subinterp);

        if (lily_load_string(subinterp, "[check]", "2") ||
            lily_load_file(subinterp, "check.lily"))
            lily_RuntimeError(s, "Not blocking content after exit.");

        lily_free_state(subinterp);
    }

    lily_return_unit(s);
}

void lily_covlib__raise_dbzerror(lily_state *s)
{
    lily_DivisionByZeroError(s, "test");
}

void lily_covlib__raise_keyerror(lily_state *s)
{
    lily_KeyError(s, "test");
}

void lily_covlib_new_Container(lily_state *s)
{
    lily_container_val *con = SUPER_Container(s);
    SET_Container__value(con, lily_arg_value(s, 0));
    lily_return_super(s);
}

void lily_covlib_Container_update(lily_state *s)
{
    lily_container_val *con = lily_arg_container(s, 0);
    SET_Container__value(con, lily_arg_value(s, 1));
    lily_return_unit(s);
}

void lily_covlib_Container_fetch(lily_state *s)
{
    lily_container_val *con = lily_arg_container(s, 0);
    lily_return_value(s, GET_Container__value(con));
}

void lily_covlib_Container_nothing(lily_state *s)
{
    lily_return_value(s, lily_arg_value(s, 0));
}

void lily_covlib_new_C2(lily_state *s)
{
    lily_container_val *con = SUPER_C2(s);
    SET_Container__value(con, lily_arg_value(s, 0));
    SET_C2__x(con, lily_arg_value(s, 1));
    lily_return_super(s);
}

void lily_covlib_C2_check(lily_state *s)
{
    lily_container_val *con = lily_arg_container(s, 0);
    lily_value *result = GET_C2__x(con);

    lily_return_value(s, result);
}

LILY_DECLARE_COVLIB_CALL_TABLE
