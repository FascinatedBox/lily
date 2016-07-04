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
    /* How much should the current number of allowed gc entries be multiplied by
       if unable to free anything. */
    uint8_t gc_multiplier;
    uint16_t argc;
    /* The initial maximum amount of entries allowed to have a gc tag before
       asking for another causes a sweep. */
    uint32_t gc_start;
    /* Should the interpreter allow the sys package to be loaded? Sandboxes and
       untrusted environments should set this to 0.
       Default: 1 */
    uint32_t allow_sys;

    uint32_t pad;
    /* This is used by the interpreter to compute hashes of a raw value for
       doing Hash collision checks. This key should be composed of exactly 16
       chars. */
    char *sipkey;
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
