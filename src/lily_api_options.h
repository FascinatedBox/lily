#ifndef LILY_API_OPTIONS_H
# define LILY_API_OPTIONS_H

# include <stdint.h>

typedef void (*lily_html_sender)(char *, void *);

/* This structure defines a series of options for initializing the interpeter.
   Defaults can be found by searching for lily_new_default_options within
   lily_parser.c. */
typedef struct lily_options_ {
    /* For now, this should be '1'. */
    uint8_t version;
    /* When the gc fails to free any values, how much should the allowed # of
       tags be multiplied by? (Clamped to 16). */
    uint8_t gc_multiplier;

    /* Should the interpreter allow 'use' of sys? */
    uint8_t allow_sys;

    /* Should the interpreter free options when it's done? */
    uint8_t free_options;

    /* How many gc entries should the vm allow before asking for another causes
       a sweep? */
    uint32_t gc_start;

    uint32_t pad2;
    int argc;
    /* This is made available as sys.argv when sys is imported. By default,
       this is NULL and sys.argv is empty. */
    char **argv;
    /* Lily will call the html impl with this as the data part. This
       can be NULL if it's not needed.
       This is used by mod_lily to hold Apache's request_rec. */
    void *data;
    /* This is the function that will be called when tagged data is seen. The
       first argument will be the data parameter above. */
    lily_html_sender html_sender;
} lily_options;

lily_options *lily_new_default_options(void);
void lily_free_options(lily_options *);

#endif
