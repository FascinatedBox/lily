#ifndef LILY_SEED_SYMTAB_H
# define LILY_SEED_SYMTAB_H

# include "lily_pkg.h"
# include "lily_vm.h"

/* Sync name order with SYM_CLASS_* #defines in lily_symtab.h */
typedef const struct {
    char *name;
    int is_refcounted;
} class_seed;

class_seed class_seeds[7] =
{
    {"integer",  0},
    {"number",   0},
    {"str",      1},
    {"function", 0},
    {"object",   1},
    {"method",   1},
    {"list",     1}
};

static const char *keywords[] = {
    "if",
    "elif",
    "else",
    "return",
    "while",
    "continue",
    "break",
    "show",
    "__line__",
    "__file__",
    "__method__",
    "for",
    "do"
};

void lily_builtin_print(lily_vm_state *, uintptr_t *, int);
void lily_builtin_printfmt(lily_vm_state *, uintptr_t *, int);

static lily_func_seed print =
    {"print", 1, 0, lily_builtin_print, {-1, SYM_CLASS_STR}};
static lily_func_seed printfmt =
    {"printfmt", 2, 1, lily_builtin_printfmt, {-1, SYM_CLASS_STR, SYM_CLASS_OBJECT}};
static lily_func_seed *builtin_seeds[] = {&print, &printfmt};
#define NUM_BUILTIN_SEEDS 2

#endif
