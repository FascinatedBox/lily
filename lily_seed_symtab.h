#ifndef LILY_SEED_SYMTAB_H
# define LILY_SEED_SYMTAB_H

# include "lily_pkg.h"
# include "lily_vm.h"

/* Sync name order with SYM_CLASS_* #defines in lily_symtab.h */
typedef const struct {
    char *name;
    int is_refcounted;
    uint64_t shorthash;
} class_seed;

class_seed class_seeds[8] =
{
    {"integer",  0, 32199642103180905},
    {"number",   0, 125779768604014},
    {"str",      1, 7500915},
    {"function", 0, 7957695015192261990},
    {"object",   1, 127970252055151},
    {"method",   1, 110429656606061},
    {"list",     1, 1953720684},
    /* * is the name of the template class. This was chosen because it's not a
       valid name so the user can't directly declare members of it. The hash is
       also invalid too. */
    {"*",        0, 0}
};

typedef const struct {
    char *name;
    uint64_t shorthash;
} keyword_seed;

keyword_seed keywords[] = {
    {"if",         26217},
    {"elif",       1718185061},
    {"else",       1702063205},
    {"return",     121437875889522},
    {"while",      435610544247},
    {"continue",   7310870969309884259},
    {"break",      461195539042},
    {"show",       2003789939},
    {"__line__",   6872323081280184159},
    {"__file__",   6872323072689856351},
    {"__method__", 7237117975334838111},
    {"for",        7499622},
    {"do",         28516}
};

void lily_builtin_print(lily_vm_state *, uintptr_t *, int);
void lily_builtin_printfmt(lily_vm_state *, uintptr_t *, int);

static lily_func_seed print =
    {"print", lily_builtin_print,
        {SYM_CLASS_FUNCTION, 2, 0, -1, SYM_CLASS_STR}};
static lily_func_seed printfmt =
    {"printfmt", lily_builtin_printfmt,
        {SYM_CLASS_FUNCTION, 3, SIG_IS_VARARGS, -1, SYM_CLASS_STR, SYM_CLASS_OBJECT}};

static lily_func_seed *builtin_seeds[] = {&print, &printfmt};
#define NUM_BUILTIN_SEEDS 2

#endif
