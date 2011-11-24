#ifndef LILY_SEED_SYMTAB_H
# define LILY_SEED_SYMTAB_H

# include "lily_symtab.h"

/* Sync name order with SYM_CLASS_* #defines in lily_symtab.h */
char *class_seeds[] = {
    "integer",
    "number",
    "str",
    "function"
};

static const struct var_entry {
    char *name;
    int class_id;
    int num_args;
} var_seeds[] =
{
    {"print", SYM_CLASS_FUNCTION, 1},
    /* All code outside of functions is stuffed here, and at the end of parsing,
       this function is called. */
    {"@main", SYM_CLASS_FUNCTION, 0}
};

/* It's important to know this id, because symbols after it have their name
   malloc'd, so it'll need to be free'd. */
# define MAIN_FUNC_ID 1

#endif
