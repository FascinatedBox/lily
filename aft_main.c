#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "lily_interp.h"

/* aft_main.c
   aft is for allocation failure test. This processes a .ly file the way lily_fs
   would, but also takes a number of allocations to allow. Once the limit is
   hit, any following mallocs/reallocs will be rejected.
   This tool exists to make sure that any particular allocation failing will not
   cause a segfault/double free/memory leak. */

#define ST_ALLOCATED 0
#define ST_DELETED   1

typedef struct aft_entry_ {
    int line;
    int id;
    int status;
    char *filename;
    char *funcname;
    void *block;
    struct aft_entry_ *prev;
    struct aft_entry_ *next;
} aft_entry;

aft_entry *start = NULL;
aft_entry *current = NULL;
aft_entry *end = NULL;

/* The number of mallocs should match the number of frees. */
int free_count = 0;
int malloc_count = 0;
/* This is backward so that I don't accidentally use the wrong one. */
int count_reallocs = 0;
int warning_count = 0;

/* How many allocs to allow before rejecting them. */
int allowed_allocs = 0;

void *aft_malloc(char *filename, int line, size_t size)
{
    if (malloc_count + count_reallocs >= allowed_allocs) {
        fprintf(stderr, "[aft]: malloc via %s:%d for size %lu REJECTED.\n",
                filename, line, size);
        return NULL;
    }

    void *block = malloc(size);
    if (block == NULL) {
        /* Report the size and location, in case it's an unreasonable size. */
        fprintf(stderr, "[aft]: fatal: malloc via %s:%d for size %lu FAILED.\n",
                filename, line, size);
        exit(EXIT_FAILURE);
    }
    aft_entry *entry = malloc(sizeof(aft_entry));
    if (entry == NULL) {
        fprintf(stderr, "[aft]: fatal: Out of memory for aft_entry.\n");
        exit(EXIT_FAILURE);
    }
    entry->filename = filename;
    entry->line = line;
    entry->block = block;
    entry->next = NULL;
    entry->status = ST_ALLOCATED;
    entry->id = malloc_count;

    if (start == NULL)
        start = entry;

    if (end != NULL)
        end->next = entry;

    end = entry;
    fprintf(stderr, "[aft]: malloc #%d (%p) via %s:%d for size %lu OK.\n",
            malloc_count+1, block, filename, line, size);
    malloc_count++;

    return block;
}

void *aft_realloc(char *filename, int line, void *oldptr, size_t newsize)
{
    if (malloc_count + count_reallocs >= allowed_allocs) {
        fprintf(stderr, "[aft]: realloc via %s:%d for size %lu REJECTED.\n",
                filename, line, newsize);
        return NULL;
    }

    aft_entry *search = start;
    while (search != NULL) {
        if (search->block == oldptr)
            break;
        search = search->next;
    }

    if (search == NULL) {
        fprintf(stderr, "[aft]: warning: realloc #%d via %s:%d has foreign oldptr!\n",
                count_reallocs, filename, line);
        warning_count++;
        return NULL;
    }

    if (search->status == ST_DELETED) {
        fprintf(stderr, "[aft]: warning: realloc #%d via %s:%d targets deleted block.\n",
                count_reallocs, filename, line);
        warning_count++;
        return NULL;
    }

    search->block = realloc(search->block, newsize);
    if (search->block == NULL) {
        fprintf(stderr, "[aft]: fatal: realloc via %s:%d for size %lu failed.\n",
                filename, line, newsize);
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "[aft]: realloc #%d via %s:%d OK. Result is %p.\n",
            count_reallocs+1, filename, line, search->block);
    count_reallocs++;
    return search->block;
}

void aft_free(char *filename, int line, void *ptr)
{
    if (ptr == NULL) {
        /* This is a no-op, but report anyway for completeness. */
        fprintf(stderr, "[aft]: null free from %s:%d.\n", filename, line);
        return;
    }

    aft_entry *search = start;
    int i = 1;
    while (search != NULL) {
        if (search->block == ptr)
            break;
        i++;
        search = search->next;
    }

    if (search == NULL) {
        fprintf(stderr, "[aft]: warning: invalid free (%p) from %s:%d!\n", ptr,
                filename, line);
        warning_count++;
        return;
    }

    if (search->status == ST_DELETED) {
        fprintf(stderr, "[aft]: warning: double free (%p) from %s:%d!\n", ptr,
                filename, line);
        warning_count++;
        return;
    }
 
    search->status = ST_DELETED;
    fprintf(stderr, "[aft]: free block #%d (%p) from %s:%d.\n", i, ptr,
            filename, line);
    free(ptr);
    free_count++;
}

void lily_impl_debugf(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
}

void lily_impl_send_html(char *htmldata)
{

}

void show_stats()
{
    if (malloc_count != free_count) {
        aft_entry *search = start;
        int i = 1;
        while (search != NULL) {
            if (search->status == ST_ALLOCATED) {
                fprintf(stderr, "[aft]: warning: Block #%d (%p) from %s:%d was not free'd.\n",
                        i, search->block, search->filename, search->line);
                warning_count++;
            }
            i++;
            search = search->next;
        }
    }

    fprintf(stderr,
            "[aft]: Stats: %d/%d malloc/free. %d reallocs, %d total allocs.\n",
            malloc_count, free_count, count_reallocs,
            malloc_count + count_reallocs);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fputs("Usage : lily_aft <allocs> <filename>\n", stderr);
        exit(EXIT_FAILURE);
    }

    allowed_allocs = atoi(argv[1]);
    lily_interp *interp = lily_new_interp();
    if (interp == NULL) {
        fputs("ErrNoMemory: No memory to alloc interpreter.\n", stderr);
        show_stats();
        exit(EXIT_FAILURE);
    }

    if (lily_parse_file(interp, argv[2]) == 0) {
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
    show_stats();

    if (malloc_count == free_count && warning_count == 0)
        exit(EXIT_SUCCESS);
    else
        exit(EXIT_FAILURE);
}
