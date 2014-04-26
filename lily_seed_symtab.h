#ifndef LILY_SEED_SYMTAB_H
# define LILY_SEED_SYMTAB_H

# include "lily_pkg.h"
# include "lily_vm.h"
# include "lily_gc.h"

/* Sync name order with SYM_CLASS_* #defines in lily_symtab.h */
typedef const struct {
    char *name;
    int is_refcounted;
    int template_count;
    gc_marker_func gc_marker;
} class_seed;

class_seed class_seeds[9] =
{
    {"integer",  0, 0, NULL},
    {"number",   0, 0, NULL},
    {"str",      1, 0, NULL},
    {"function", 0, 0, NULL},
    {"object",   1, 0, &lily_gc_object_marker},
    {"method",   1, 0, NULL},
    {"list",     1, 1, &lily_gc_list_marker},
    {"hash",     1, 2, &lily_gc_hash_marker},
    /* * is the name of the template class. This was chosen because it's not a
       valid name so the user can't directly declare members of it. */
    {"*",        0, 0, NULL}
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
