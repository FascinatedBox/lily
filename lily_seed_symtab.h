#ifndef LILY_SEED_SYMTAB_H
# define LILY_SEED_SYMTAB_H

# include "lily_symtab.h"
# include "lily_opcode.h"

typedef struct method_seed_t {
    lily_method_op method_op;
    lily_opcode vm_opcode;
    int rhs_id;
    int result_id;
    const struct method_seed_t *next;
} method_seed;

static const method_seed integer_seeds[2] = 
{
    /* To explain the first one:
     * integer plus integer results in integer. method_plus will be used for the
     * lookup, and o_integer_add will be the opcode. */
    {method_plus, o_integer_add, SYM_CLASS_INTEGER, SYM_CLASS_INTEGER,
        &integer_seeds[1]},
    {method_plus, o_number_add, SYM_CLASS_NUMBER, SYM_CLASS_NUMBER, NULL}
};

static const method_seed number_seeds[3] = 
{
    {method_plus, o_number_add, SYM_CLASS_INTEGER, SYM_CLASS_NUMBER,
        &number_seeds[1]},
    {method_plus, o_number_add, SYM_CLASS_NUMBER, SYM_CLASS_NUMBER, NULL}
};

/* Sync name order with SYM_CLASS_* #defines in lily_symtab.h */
static const struct {
    char *name;
    const method_seed *methods;
} class_seeds[4] =
{
    {"function", NULL},
    {"str", NULL},
    {"integer", integer_seeds},
    {"number", number_seeds}
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
