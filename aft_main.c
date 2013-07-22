#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "lily_parser.h"

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

#define OPT_SHOW_ALLOC_INFO   0x1
#define OPT_SHOW_SYMTAB       0x2
int aft_options = 0;

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
    /* Always show rejections. Sometimes, there might be multiple ones from
       some function trying multiple allocs, then checking them all at once.
       But rejections are useful for determining where a problem starts. */
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
    /* Make this optional, since it can get -very- verbose. */
    if (aft_options & OPT_SHOW_ALLOC_INFO)
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
    aft_entry *result = NULL;

    /* Occasionally, malloc will yield a block that was previously free'd. It's
       important to return the LAST block matching the pointer, because that
       will be the block not yet deleted. */
    while (search != NULL) {
        if (search->block == oldptr) {
            result = search;
            if (search->status != ST_DELETED)
                break;
        }
        search = search->next;
    }

    search = result;

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

    if (aft_options & OPT_SHOW_ALLOC_INFO)
        fprintf(stderr, "[aft]: realloc #%d via %s:%d OK. Result is %p.\n",
                count_reallocs+1, filename, line, search->block);
    count_reallocs++;
    return search->block;
}

void aft_free(char *filename, int line, void *ptr)
{
    if (ptr == NULL) {
        /* This is a no-op, but report anyway for completeness. */
        if (aft_options & OPT_SHOW_ALLOC_INFO)
            fprintf(stderr, "[aft]: null free from %s:%d.\n", filename, line);
        return;
    }

    aft_entry *search = start;
    aft_entry *result = NULL;
    int i = 1;
    int last_i = 0;

    while (search != NULL) {
        if (search->block == ptr) {
            /* Like with realloc, this needs to yield the last valid block. */
            result = search;
            last_i = i;
        }
        i++;
        search = search->next;
    }

    i = last_i;
    search = result;

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
    if (aft_options & OPT_SHOW_ALLOC_INFO)
        fprintf(stderr, "[aft]: free block #%d (%p) from %s:%d.\n", i, ptr,
                filename, line);
    free(ptr);
    free_count++;
}

/* There's no supression for this because it won't get called if --show-symtab
   isn't passed. */
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

    /* It's only ever failure if malloc/free counts don't match. */
    int exit_code;
    if (malloc_count == free_count && warning_count == 0)
        exit_code = EXIT_SUCCESS;
    else
        exit_code = EXIT_FAILURE;

    exit(exit_code);
}

void usage()
{
    fputs("Usage : aft [options] <allocs> <filename>\n", stderr);
    fputs("Where options are:\n", stderr);

    fputs("\t--no-alloc-limit: Disable allocation checking.\n", stderr);
    fputs("\taft will still give stats at the end, so this can be used to\n", stderr);
    fputs("\tdetermine how many allocs are needed for a program to work.\n", stderr);
    fputs("\tIf this is used, an alloc count is not needed.\n\n", stderr);

    fputs("\t--show-symtab: Have the interpreter show symtab before executing.\n", stderr);
    fputs("\tThis can help determine if bad code is being generated.\n", stderr);
    fputs("\tAs a warning, this can be fairly verbose.\n\n", stderr);

    fputs("\t--show-alloc-info: Show verbose alloc information from aft.\n", stderr);
    fputs("\tAs a warning, this can get extremely verbose in some cases.\n", stderr);

    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    char *filename = NULL;
    int parse_opts = 0;
    int i;

    for (i = 1;i < argc;i++) {
        char *arg = argv[i];
        if (strcmp(arg, "--no-alloc-limit") == 0)
            allowed_allocs = INT_MAX;
        else if (strcmp(arg, "--show-alloc-info") == 0)
            aft_options |= OPT_SHOW_ALLOC_INFO;
        else if (strcmp(arg, "--show-symtab") == 0)
            parse_opts |= POPT_SHOW_SYMTAB;
        else {
            if (allowed_allocs == 0) {
                allowed_allocs = atoi(argv[i]);
                if (allowed_allocs == 0)
                    usage();
            }
            else if (filename == NULL)
                filename = argv[i];
            else
                usage();
        }
    }

    if (allowed_allocs == 0 || filename == NULL)
        usage();

    lily_parse_state *parser = lily_new_parse_state(parse_opts);
    /* The alloc count was probably low. Show stats and give up. */
    if (parser == NULL)
        show_stats();

    if (lily_parse_file(parser, filename) == 0) {
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

    lily_free_parse_state(parser);
    /* This will always exit. */
    show_stats();
    /* Never reached, but keeps gcc happy. */
    exit(EXIT_SUCCESS);
}
