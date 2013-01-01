#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "lily_interp.h"

/* fs_main.c :
 * Since lily will be run from a server for most of the time, this emulates a
 * server...kind of. */
void lily_impl_debugf(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}

void lily_impl_send_html(char *htmldata)
{
    fputs(htmldata, stdout);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fputs("Usage : lily_fs <filename>\n", stderr);
        exit(EXIT_FAILURE);
    }

    lily_interp *interp = lily_new_interp();
    if (interp == NULL) {
        fputs("ErrNoMemory: No memory to alloc interpreter.\n", stderr);
        exit(EXIT_FAILURE);
    }

    if (lily_parse_file(interp, argv[1]) == 0) {
        lily_parse_state *parser = interp->parser;
        lily_excep_data *error = interp->error;
        fprintf(stderr, "%s", lily_name_for_error(error->error_code));
        if (error->message)
            fprintf(stderr, ": %s", error->message);
        else
            fputc('\n', stderr);

        if (parser->mode == pm_parse) {
            int line_num;
            if (interp->error->line_adjust == 0)
                line_num = interp->parser->lex->line_num;
            else
                line_num = interp->error->line_adjust;

            fprintf(stderr, "Where: File \"%s\" at line %d\n",
                    interp->parser->lex->filename, line_num);
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
        exit(EXIT_FAILURE);
    }

    lily_free_interp(interp);
    exit(EXIT_SUCCESS);
}
