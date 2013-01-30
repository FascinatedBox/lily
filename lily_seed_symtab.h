#ifndef LILY_SEED_SYMTAB_H
# define LILY_SEED_SYMTAB_H

# include "lily_pkg.h"

/* Sync name order with SYM_CLASS_* #defines in lily_symtab.h */
char *class_seeds[] = {
    "integer",
    "number",
    "str",
    "function",
    "object",
    "method",
    "list"
};

static const char *keywords[] = {
    "if",
    "elif",
    "else",
    "return"
};

void lily_builtin_print(int, lily_sym **);
void lily_builtin_printfmt(int, lily_sym **);

static lily_func_seed print =
    {"print", 1, 0, lily_builtin_print, {-1, SYM_CLASS_STR}};
static lily_func_seed printfmt =
    {"printfmt", 2, 1, lily_builtin_printfmt, {-1, SYM_CLASS_STR, SYM_CLASS_OBJECT}};
static lily_func_seed *builtin_seeds[] = {&print, &printfmt};
#define NUM_BUILTIN_SEEDS 2

#endif
