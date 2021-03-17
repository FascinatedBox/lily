#include <stdlib.h>
#include <string.h>

#include "lily.h"
#include "lily_parser.h"

#ifdef _WIN32
# define SLASH "\\"
#else
# define SLASH "/"
#endif

typedef struct {
    lily_msgbuf *bootcode_msgbuf;
    const char *path;
    lily_state *s;
    lily_config config;
    int pass_count;
    int fail_count;
    int skip_count;
} test_data;

#define TEST(dir, target) \
"test" SLASH dir SLASH target ".lily"

const char *all_test_paths[] =
{
    TEST("syntax",      "test_basics"),
    TEST("method",      "test_boolean"),
    TEST("method",      "test_byte"),
    TEST("method",      "test_bytestring"),
    TEST("method",      "test_file"),
    TEST("method",      "test_hash"),
    TEST("method",      "test_integer"),
    TEST("method",      "test_list"),
    TEST("method",      "test_option"),
    TEST("method",      "test_result"),
    TEST("method",      "test_string"),
    TEST("prelude",     "test_pkg_coroutine"),
    TEST("prelude",     "test_pkg_fs"),
    TEST("prelude",     "test_pkg_introspect"),
    TEST("prelude",     "test_pkg_math"),
    TEST("prelude",     "test_pkg_random"),
    TEST("prelude",     "test_pkg_subprocess"),
    TEST("prelude",     "test_pkg_sys"),
    TEST("prelude",     "test_pkg_time"),
    TEST("call",        "test_call_pipe"),
    TEST("call",        "test_keyargs"),
    TEST("call",        "test_optargs"),
    TEST("call",        "test_varargs"),
    TEST("class",       "test_class"),
    TEST("closure",     "test_verify_closures"),
    TEST("constant",    "test_verify_constant"),
    TEST("coroutine",   "test_verify_coroutine"),
    TEST("enum",        "test_enum"),
    TEST("exception",   "test_exception"),
    TEST("format",      "test_verify_format"),
    TEST("forward",     "test_forward"),
    TEST("gc",          "test_verify_gc"),
    TEST("import",      "test_import"),
    TEST("lambda",      "test_lambda"),
    TEST("manifest",    "test_manifest"),
    TEST("rewind",      "test_verify_rewind"),
    TEST("template",    "test_verify_template"),
    TEST("types",       "test_generics"),
    TEST("types",       "test_inference"),
    TEST("types",       "test_narrowing"),
    TEST("types",       "test_quantification"),
    TEST("types",       "test_scoop"),
    TEST("types",       "test_variance"),
    TEST("coverage",    "test_dynaload"),
    TEST("coverage",    "test_verify_coverage"),
    TEST("call",        "test_bad_call"),
    TEST("call",        "test_bad_keyargs"),
    TEST("call",        "test_bad_optargs"),
    TEST("class",       "test_bad_class"),
    TEST("enum",        "test_bad_enum"),
    TEST("exception",   "test_bad_exception"),
    TEST("forward",     "test_bad_forward"),
    TEST("import",      "test_bad_import"),
    TEST("lambda",      "test_bad_lambda"),
    TEST("manifest",    "test_bad_manifest"),
    TEST("syntax",      "test_bad_syntax"),
    TEST("syntax",      "test_bad_token"),
    TEST("sandbox",     "test_verify_sandbox"),
    TEST("benchmark",   "test_benchmark"),
    NULL,
};

static void clear_line(void)
{
    fputs("                                                                 \r",
          stdout);
    /* Make sure there's no output queued up. It makes which test failed easier
       to spot. The other writers (except the final tally) clear it for the
       same reason. */
    fflush(stdout);
}

static void log_test_total(test_data *td)
{
    clear_line();

    int total = td->pass_count + td->fail_count + td->skip_count;
    fprintf(stdout, "%d tests passed, %d failed, %d skipped, %d total.\n",
            td->pass_count, td->fail_count, td->skip_count, total);
}

static void log_start_test(const char *path)
{
    clear_line();
    fprintf(stdout, "Running test file %s.\r", path);
    fflush(stdout);
}

static void log_error(test_data *td)
{
    clear_line();
    fputs(lily_error_message(td->s), stdout);
    fflush(stdout);
}

static void run_test_code(test_data *td)
{
    lily_state *s = td->s;
    /* All testing methods should be in one testing class. That testing class
       should be the only one that starts with Test. */
    lily_parse_state *parser = s->gs->parser;
    lily_module_entry *main_module = parser->main_module;
    lily_class *cls = main_module->class_chain;
    lily_class *target_cls = NULL;

    while (cls) {
        if (strncmp(cls->name, "Test", 4) == 0) {
            target_cls = cls;
            break;
        }

        cls = cls->next;
    }

    if (target_cls == NULL) {
        /* Some files don't have any tests to run. If they parse, they pass. For
           those files, don't throw in any bootcode. */
        td->pass_count++;
        return;
    }

    const char *bootcode = lily_mb_sprintf(td->bootcode_msgbuf,
            "define run_tests: Tuple[Integer, Integer, Integer] {\n"
            "    var v = %s()\n"
            "    v.run_tests()\n"
            "    return <[v.pass_count, v.fail_count, v.skip_count]>\n"
            "}\n", cls->name);

    lily_load_string(s, "[test]", bootcode);
    lily_parse_content(s);

    lily_function_val *test_fn = lily_find_function(s, "run_tests");
    lily_value *result = lily_call_result(s);

    lily_call_prepare(s, test_fn);
    lily_call(s, 0);

    lily_container_val *test_con = lily_as_container(result);

    int pass = (int)lily_as_integer(lily_con_get(test_con, 0));
    int fail = (int)lily_as_integer(lily_con_get(test_con, 1));
    int skip = (int)lily_as_integer(lily_con_get(test_con, 2));

    td->pass_count += pass;
    td->fail_count += fail;
    td->skip_count += skip;
}

void run_test(test_data *td)
{
    lily_config_init(&td->config);
    td->s = lily_new_state(&td->config);

    log_start_test(td->path);

    lily_state *s = td->s;
    lily_load_file(s, td->path);

    if (lily_parse_content(s))
        run_test_code(td);
    else {
        td->fail_count++;
        log_error(td);
    }

    lily_free_state(s);
}

void init_test_data(test_data *td)
{
    td->bootcode_msgbuf = lily_new_msgbuf(128);
    td->s = NULL;
    lily_config_init(&td->config);
    td->pass_count = 0;
    td->fail_count = 0;
    td->skip_count = 0;
}

void free_test_data(test_data *td)
{
    lily_free_msgbuf(td->bootcode_msgbuf);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    int i;
    test_data td;

    init_test_data(&td);

    for (i = 0;all_test_paths[i] != NULL;i++) {
        td.path = all_test_paths[i];
        run_test(&td);
    }

    log_test_total(&td);
    free_test_data(&td);

    int exit_code = EXIT_SUCCESS;

    if (td.fail_count)
        exit_code = EXIT_FAILURE;

    exit(exit_code);
}
