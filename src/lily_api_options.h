#ifndef LILY_API_OPTIONS_H
# define LILY_API_OPTIONS_H

# include <stdint.h>

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
    /* This is used by the interpreter to compute hashes of a raw value for
       doing Hash collision checks. This key should be composed of exactly 16
       chars. */
    char *sipkey;
    /* This is made available as sys.argv when sys is imported. By default,
       this is NULL and sys.argv is empty. */
    char **argv;
    /* Lily will call lily_impl_puts with this as the data part. This
       can be NULL if it's not needed.
       This is used by mod_lily to hold Apache's request_rec. */
    void *data;
} lily_options;

lily_options *lily_new_default_options(void);
void lily_free_options(lily_options *);

#endif
