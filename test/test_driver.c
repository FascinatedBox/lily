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

typedef struct {
    const char *dir;
    const char **targets;
} test_group;

const char *benchmark_targets[] = {
    "test_benchmark.lily",
    NULL,
};

const char *call_targets[] = {
    "test_bad_call.lily",
    "test_bad_keyargs.lily",
    "test_bad_optargs.lily",
    "test_call_pipe.lily",
    "test_keyargs.lily",
    "test_optargs.lily",
    "test_varargs.lily",
    NULL,
};

const char *class_targets[] = {
    "test_bad_class.lily",
    "test_class.lily",
    NULL,
};

const char *closure_targets[] = {
    "test_verify_closures.lily",
    NULL,
};

const char *coroutine_targets[] = {
    "test_verify_coroutine.lily",
    NULL,
};

const char *coverage_targets[] = {
    "test_dynaload.lily",
    "test_verify_coverage.lily",
    NULL,
};

const char *enum_targets[] = {
    "test_bad_enum.lily",
    "test_enum.lily",
    NULL,
};

const char *exception_targets[] = {
    "test_bad_exception.lily",
    "test_exception.lily",
    NULL,
};

const char *format_targets[] = {
    "test_verify_format.lily",
    NULL,
};

const char *forward_targets[] = {
    "test_bad_forward.lily",
    "test_forward.lily",
    NULL,
};

const char *gc_targets[] = {
    "test_verify_gc.lily",
    NULL,
};

const char *import_targets[] = {
    "test_bad_import.lily",
    "test_import.lily",
    NULL,
};

const char *lambda_targets[] = {
    "test_bad_lambda.lily",
    "test_lambda.lily",
    NULL,
};

const char *method_targets[] = {
    "test_boolean.lily",
    "test_byte.lily",
    "test_bytestring.lily",
    "test_file.lily",
    "test_hash.lily",
    "test_integer.lily",
    "test_list.lily",
    "test_option.lily",
    "test_result.lily",
    "test_string.lily",
    NULL,
};

const char *prelude_targets[] = {
    "test_pkg_math.lily",
    "test_pkg_random.lily",
    "test_pkg_sys.lily",
    "test_pkg_time.lily",
    NULL,
};

const char *rewind_targets[] = {
    "test_verify_rewind.lily",
    NULL,
};

const char *sandbox_targets[] = {
    "test_verify_sandbox.lily",
    NULL,
};

const char *syntax_targets[] = {
    "test_bad_syntax.lily",
    "test_bad_token.lily",
    "test_basics.lily",
    NULL,
};

const char *template_targets[] = {
    "test_verify_template.lily",
    NULL,
};

const char *types_targets[] = {
    "test_generics.lily",
    "test_inference.lily",
    "test_narrowing.lily",
    "test_quantification.lily",
    "test_scoop.lily",
    "test_variance.lily",
    NULL,
};

test_group all_groups[] = {
    {"call",      call_targets},
    {"class",     class_targets},
    {"closure",   closure_targets},
    {"coroutine", coroutine_targets},
    {"coverage",  coverage_targets},
    {"enum",      enum_targets},
    {"exception", exception_targets},
    {"format",    format_targets},
    {"forward",   forward_targets},
    {"gc",        gc_targets},
    {"import",    import_targets},
    {"lambda",    lambda_targets},
    {"method",    method_targets},
    {"prelude",   prelude_targets},
    {"rewind",    rewind_targets},
    {"sandbox",   sandbox_targets},
    {"syntax",    syntax_targets},
    {"template",  template_targets},
    {"types",     types_targets},
    {"benchmark", benchmark_targets},
    {NULL,        NULL},
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
    test_data td;
    test_group g;
    const char *target;
    lily_msgbuf *dir_msgbuf = lily_new_msgbuf(128);

    init_test_data(&td);

    int i, j;

    for (i = 0, g = all_groups[i];
         g.dir != NULL;
         i++, g = all_groups[i]) {

        for (j = 0, target = g.targets[j];
             target != NULL;
             j++, target = g.targets[j]) {
            td.path = lily_mb_sprintf(dir_msgbuf, "test%s%s%s%s", SLASH, g.dir,
                                      SLASH, target);
            run_test(&td);
        }
    }

    log_test_total(&td);
    free_test_data(&td);
    lily_free_msgbuf(dir_msgbuf);

    int exit_code = EXIT_SUCCESS;

    if (td.fail_count)
        exit_code = EXIT_FAILURE;

    exit(exit_code);
}
