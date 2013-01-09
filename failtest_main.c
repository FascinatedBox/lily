#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "lily_parser.h"

/* failtest_main.c :
   This uses the lexer's repl- */

void lily_impl_debugf(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}

void lily_impl_send_html(char *htmldata)
{

}

static char *tests[][2] =
{
    {"Testing an assignment of the wrong type...",
     "integer a\n"
     "a = \"\"\n"},
    {"Testing a call with an arg of the wrong type...",
     "method m(integer a):nil {\n"
     "}\n"
     "m(\"str\")\n"},
    {"Testing a redeclaration...",
     "str a\n"
     "integer a\n"},
    {"Testing an incorrect starting token...",
     "+\n"}
};

int main(int argc, char **argv)
{
    if (argc != 1) {
        fputs("Usage : lily_failtest\n", stderr);
        exit(EXIT_FAILURE);
    }

    int i, num_tests, tests_failed;

    num_tests = sizeof(tests) / sizeof(tests[0]);
    tests_failed = 0;
    for (i = 0;i < num_tests;i++) {
        fprintf(stderr, "[%d/%d] %s", i+1, num_tests, tests[i][0]);
        /* todo: It would probably be better to reset a single parser, instead
           of creating and destroying multiple ones. */
        lily_parse_state *parser = lily_new_parse_state();
        if (parser == NULL) {
            fputs("ErrNoMemory: No memory to alloc interpreter.\n", stderr);
            exit(EXIT_FAILURE);
        }
        if (lily_parse_string(parser, tests[i][1]) == 0) {
            tests_failed++;
            fprintf(stderr, "failed.\n");
            lily_raiser *raiser = parser->raiser;
            fprintf(stderr, "%s", lily_name_for_error(raiser->error_code));
            if (raiser->message)
                fprintf(stderr, ": %s", raiser->message);
            else
                fputc('\n', stderr);

            if (parser->mode == pm_parse) {
                int line_num;
                if (raiser->line_adjust == 0)
                    line_num = parser->lex->line_num;
                else
                    line_num = raiser->line_adjust;

                fprintf(stderr, "Where: File \"%s\" at line %d\n",
                        parser->lex->filename, line_num);
            }
            else if (parser->mode == pm_execute) {
                lily_vm_stack_entry **vm_stack;
                lily_vm_stack_entry *entry;
                int i;

                vm_stack = parser->vm->method_stack;
                fprintf(stderr, "Traceback:\n");
                for (i = parser->vm->method_stack_pos-1;i >= 0;i--) {
                    entry = vm_stack[i];
                    fprintf(stderr, "    Method \"%s\" at line %d.\n",
                            ((lily_var *)entry->method)->name, entry->line_num);
                }
            }
        }
        else
            fprintf(stderr, "passed.\n");

        /* Use this to group a test with its error message. */
        fputc('\n', stderr);
        lily_free_parse_state(parser);
    }

    fprintf(stderr, "Tests failed: %d / %d.\n", tests_failed, num_tests);
    if (tests_failed == num_tests)
        exit(EXIT_FAILURE);
    else
        exit(EXIT_SUCCESS);
}
