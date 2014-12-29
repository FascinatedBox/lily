#include <ctype.h>
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
    int status;
    char *filename;
    void *block;
    struct aft_entry_ *prev;
    struct aft_entry_ *next;
} aft_entry;

#define OPT_SHOW_ALLOC_INFO   0x1
int aft_options = 0;

aft_entry *start = NULL;
aft_entry *current = NULL;
aft_entry *end = NULL;

int first_pass_count = 0;

/* The number of mallocs should match the number of frees. */
int free_count = 0;
int malloc_count = 0;
/* This is backward so that I don't accidentally use the wrong one. */
int count_reallocs = 0;
int warning_count = 0;

/* How many allocs to allow before rejecting them. */
int allowed_allocs = 0;

#define LOG(message, args...) \
if (aft_options & OPT_SHOW_ALLOC_INFO) \
    fprintf(stderr, message, args);

static aft_entry *make_entry(char *filename, int line)
{
    aft_entry *entry = malloc(sizeof(aft_entry));
    if (entry == NULL) {
        fprintf(stderr, "[aft]: fatal: Out of memory for aft_entry.\n");
        exit(EXIT_FAILURE);
    }
    entry->filename = filename;
    entry->line = line;
    entry->block = NULL;
    entry->next = NULL;
    entry->status = ST_ALLOCATED;

    if (start == NULL)
        start = entry;

    if (end != NULL)
        end->next = entry;

    end = entry;
    return entry;
}

void *aft_malloc(char *filename, int line, size_t size)
{
    /* Always show rejections. Sometimes, there might be multiple ones from
       some function trying multiple allocs, then checking them all at once.
       But rejections are useful for determining where a problem starts. */
    if (malloc_count + count_reallocs >= allowed_allocs) {
        LOG("[aft]: malloc via %s:%d for size %lu REJECTED.\n", filename, line,
            size);
        return NULL;
    }

    void *block = malloc(size);
    if (block == NULL) {
        /* Report the size and location, in case it's an unreasonable size. */
        fprintf(stderr, "[aft]: fatal: malloc via %s:%d for size %lu FAILED.\n",
                filename, line, size);
        exit(EXIT_FAILURE);
    }

    aft_entry *entry = make_entry(filename, line);
    entry->block = block;

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
        LOG("[aft]: realloc via %s:%d for size %lu REJECTED.\n",
                filename, line, newsize);
        return NULL;
    }

    aft_entry *search = start;
    aft_entry *result = NULL;

    /* Since aft frees as it goes, malloc can return the same block multiple
       times. Rather than filter these entries out (the correct way), this
       instead looks until it finds the one currently allocated.
       There can only be one block at a particular address at a time, so this is
       a safe assertion. */
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
        /* The C standard allows for a realloc of NULL. */
        if (oldptr == NULL)
            search = make_entry(filename, line);
        else {
            fprintf(stderr,
                    "[aft]: warning: realloc #%d via %s:%d has foreign oldptr!\n",
                    count_reallocs, filename, line);

            warning_count++;
            return NULL;
        }
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

    LOG("[aft]: realloc #%d via %s:%d OK. Result is %p.\n",
        count_reallocs + 1, filename, line, search->block);

    /* In this case, the realloc is acting like malloc and adding a new block
       that will need to be free'd. */
    if (oldptr == NULL)
        malloc_count++;

    count_reallocs++;
    return search->block;
}

void aft_free(char *filename, int line, void *ptr)
{
    if (ptr == NULL)
        return;

    aft_entry *search = start;
    aft_entry *result = NULL;
    int i = 1;
    int last_i = 0;

    /* See comment about realloc's search. */
    while (search != NULL) {
        if (search->block == ptr) {
            result = search;
            last_i = i;
            if (search->status != ST_DELETED)
                break;
        }
        i++;
        search = search->next;
    }

    i = last_i;
    search = result;

    if (search == NULL) {
        fprintf(stderr, "[aft]: warning: invalid free (%p) from %s:%d!\n", ptr, filename,
                line);
        warning_count++;
        return;
    }

    if (search->status == ST_DELETED) {
        fprintf(stderr, "[aft]: warning: double free (%p) from %s:%d!\n", ptr, filename,
                line);
        warning_count++;
        return;
    }

    search->status = ST_DELETED;
    LOG("[aft]: free block #%d (%p) from %s:%d.\n", i, ptr, filename, line);
    free(ptr);
    free_count++;
}

/* This is a no-op so that each pass doesn't spam the console with info and make
   the thing slower. */
void lily_impl_puts(void *data, char *text)
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

    /* This function is only called when there is an issue.  */
    exit(EXIT_FAILURE);
}

void aft_cleanup()
{
    malloc_count = 0;
    free_count = 0;
    count_reallocs = 0;
    warning_count = 0;

    aft_entry *entry_iter = start;
    aft_entry *entry_next = NULL;
    while (entry_iter) {
        entry_next = entry_iter->next;
        free(entry_iter);
        entry_iter = entry_next;
    }

    start = NULL;
    current = NULL;
    end = NULL;
}

void usage()
{
    fputs("Usage : aft [options] (-f filename | -s string)\n", stderr);

    fputs("\t-f filename         Run the given filename.\n", stderr);
    fputs("\t-s string           Run the given string.\n", stderr);
    fputs("\t(-f and -s are mutually exclusive).\n", stderr);
    fputs("\n", stderr);
    fputs("\t-m max              Do one pass with <max> (non-zero) allocations.\n", stderr);
    fputs("\t--show-alloc-info:  Show verbose alloc information from aft.\n", stderr);
    fputs("\tAs a warning, this can get extremely verbose in some cases.\n", stderr);

    exit(EXIT_FAILURE);
}

void run_parser(int argc, char **argv, int count, int is_filename, char *text)
{
    allowed_allocs = count;

    if (allowed_allocs == INT_MAX)
        fputs("aft: Doing a first pass...", stderr);
    else
        fprintf(stderr, "aft: Running pass %d...", count);

    lily_parse_state *parser = lily_new_parse_state(NULL, argc, argv);
    if (parser != NULL) {
        if (is_filename == 1)
            lily_parse_file(parser, lm_tags, text);
        else
            lily_parse_string(parser, "[str]", lm_no_tags, text);

        lily_free_parse_state(parser);
    }

    if (malloc_count != free_count || warning_count != 0) {
        fprintf(stderr, "failed.\n", warning_count);
        show_stats();
    }
    else {
        fprintf(stderr, "ok.\n");
        if (count == INT_MAX)
            first_pass_count = malloc_count;

        aft_cleanup();
    }
}

int main(int argc, char **argv)
{
    char *name = NULL;
    int i, is_filename = 1, max = -1;

    for (i = 1;i < argc;i++) {
        char *arg = argv[i];
        if (strcmp(arg, "--show-alloc-info") == 0)
            aft_options |= OPT_SHOW_ALLOC_INFO;
        else if (strcmp(arg, "-s") == 0) {
            i++;
            if (i > argc || name)
                usage();

            is_filename = 0;
            name = argv[i];
        }
        else if (strcmp(arg, "-f") == 0) {
            i++;
            if (i > argc || name)
                usage();

            name = argv[i];
        }
        else if (strcmp(arg, "-m") == 0) {
            i++;
            if (i > argc)
                usage();

            max = atoi(argv[i]);
            if (max == 0)
                usage();
        }
    }

    if (name == NULL)
        usage();

    if (max == -1) {
        /* Start with a silly limit that won't be reached to determine the maximum,
           then go down from there. */
        run_parser(argc, argv, INT_MAX, is_filename, name);
        int count = first_pass_count - 1;
        while (count) {
            run_parser(argc, argv, count, is_filename, name);
            count--;
        }
    }
    else
        run_parser(argc, argv, max, is_filename, name);

    exit(EXIT_SUCCESS);
}
