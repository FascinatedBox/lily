#ifndef LILY_SEED_SYMTAB_H
# define LILY_SEED_SYMTAB_H

# include "lily_cls_string.h"
# include "lily_cls_list.h"
# include "lily_cls_hash.h"
# include "lily_vm.h"
# include "lily_gc.h"
# include "lily_class_funcs.h"

/* Sync name order with SYM_CLASS_* #defines in lily_symtab.h */
typedef const struct {
    char *name;
    int is_refcounted;
    int template_count;
    int flags;
<<<<<<< Updated upstream
    char *parent_name;
=======
>>>>>>> Stashed changes
    const lily_prop_seed_t *prop_seeds;
    class_setup_func setup_func;
    gc_marker_func gc_marker;
    class_eq_func eq_func;
} class_seed;

static const lily_prop_seed_t message =
    {"message", NULL,
        {SYM_CLASS_STRING}};

static const lily_prop_seed_t traceback =
    {"traceback", &message,
        {SYM_CLASS_TUPLE, SYM_CLASS_LIST, SYM_CLASS_STRING, SYM_CLASS_STRING, SYM_CLASS_INTEGER}};

/* Note: If CLS_VALID_HASH_KEY is added to other classes, the vm will need to be
         updated to hash those classes right. It will also need ErrNoSuchKey
         printing to be touched up for that. Other things may also need updating
         too. */
class_seed class_seeds[] =
{
<<<<<<< Updated upstream
    {"integer",              /* name */
     0,                      /* is_refcounted */
     0,                      /* template_count */
     CLS_VALID_HASH_KEY,     /* flags */
     NULL,                   /* parent name */
     NULL,                   /* property seeds */
     NULL,                   /* setup_func */
     NULL,                   /* gc_marker */
     &lily_integer_eq        /* eq_func */
    },

    {"double",               /* name */
     0,                      /* is_refcounted */
     0,                      /* template_count */
     CLS_VALID_HASH_KEY,     /* flags */
     NULL,                   /* parent name */
     NULL,                   /* property seeds */
     NULL,                   /* setup_func */
     NULL,                   /* gc_marker */
     &lily_double_eq         /* eq_func */
    },

    {"string",               /* name */
     1,                      /* is_refcounted */
     0,                      /* template_count */
     CLS_VALID_HASH_KEY,     /* flags */
     NULL,                   /* parent name */
     NULL,                   /* property seeds */
     lily_string_setup,      /* setup_func */
     NULL,                   /* gc_marker */
     &lily_string_eq},       /* eq_func */

    {"function",             /* name */
     0,                      /* is_refcounted */
     -1,                     /* template_count */
     0,                      /* flags */
     NULL,                   /* parent name */
     NULL,                   /* property seeds */
     NULL,                   /* setup_func */
     NULL,                   /* gc_marker */
     &lily_generic_eq},      /* eq_func */

    {"any",                  /* name */
     1,                      /* is_refcounted */
     0,                      /* template_count */
     0,                      /* flags */
     NULL,                   /* parent name */
     NULL,                   /* property seeds */
     NULL,                   /* setup_func */
     &lily_gc_any_marker,    /* gc_marker */
     &lily_any_eq},          /* eq_func */

    {"list",                /* name */
     1,                     /* is_refcounted */
     1,                     /* template_count */
     0,                     /* flags */
     NULL,                  /* parent name */
     NULL,                  /* property seeds */
     lily_list_setup,       /* setup_func */
     &lily_gc_list_marker,  /* gc_marker */
     &lily_list_eq},        /* eq_func */

    {"hash",                /* name */
     1,                     /* is_refcounted */
     2,                     /* template_count */
     0,                     /* flags */
     NULL,                  /* parent name */
     NULL,                  /* property seeds */
     lily_hash_setup,       /* setup_func */
     &lily_gc_hash_marker,  /* gc_marker */
     &lily_hash_eq},        /* eq_func */

    {"tuple",               /* name */
     1,                     /* is_refcounted */
     -1,                    /* template_count */
     0,                     /* flags */
     NULL,                  /* parent name */
     NULL,                  /* property seeds */
     NULL,                  /* setup_func */
     &lily_gc_tuple_marker, /* gc_marker */
     &lily_tuple_eq},       /* eq_func */

    {"",                    /* name */
     0,                     /* is_refcounted */
     0,                     /* template_count */
     0,                     /* flags */
     NULL,                  /* parent name */
     NULL,                  /* property seeds */
     NULL,                  /* setup_func */
     NULL,                  /* gc_marker */
     NULL},                 /* eq_func */

    {"package",             /* name */
     0,                     /* is_refcounted */
     0,                     /* template_count */
     0,                     /* flags */
     NULL,                  /* parent name */
     NULL,                  /* property seeds */
     NULL,                  /* setup_func */
     NULL,                  /* gc_marker */
     NULL},                 /* eq_func */

    {"Exception",           /* name */
     1,                     /* is_refcounted */
     0,                     /* template_count */
     0,                     /* flags */
     NULL,                  /* parent name */
     &traceback,            /* property seeds */
     NULL,                  /* setup_func */
     NULL,                  /* gc_marker */
     NULL},                 /* eq_func */

    /* SyntaxError, ImportError, and EncodingError are intentionally missing
       because they cannot be raised while the vm is running. */

    {"NoMemoryError",       /* name */
     1,                     /* is_refcounted */
     0,                     /* template_count */
     0,                     /* flags */
     "Exception",           /* parent name */
     NULL,                  /* property seeds */
     NULL,                  /* setup_func */
     NULL,                  /* gc_marker */
     NULL},                 /* eq_func */

    {"NoValueError",        /* name */
     1,                     /* is_refcounted */
     0,                     /* template_count */
     0,                     /* flags */
     "Exception",           /* parent name */
     NULL,                  /* property seeds */
     NULL,                  /* setup_func */
     NULL,                  /* gc_marker */
     NULL},                 /* eq_func */

    {"DivisionByZeroError", /* name */
     1,                     /* is_refcounted */
     0,                     /* template_count */
     0,                     /* flags */
     "Exception",           /* parent name */
     NULL,                  /* property seeds */
     NULL,                  /* setup_func */
     NULL,                  /* gc_marker */
     NULL},                 /* eq_func */

    {"RangeError",          /* name */
     1,                     /* is_refcounted */
     0,                     /* template_count */
     0,                     /* flags */
     "Exception",           /* parent name */
     NULL,                  /* property seeds */
     NULL,                  /* setup_func */
     NULL,                  /* gc_marker */
     NULL},                 /* eq_func */

    {"BadTypecastError",    /* name */
     1,                     /* is_refcounted */
     0,                     /* template_count */
     0,                     /* flags */
     "Exception",           /* parent name */
     NULL,                  /* property seeds */
     NULL,                  /* setup_func */
     NULL,                  /* gc_marker */
     NULL},                 /* eq_func */

    {"NoReturnError",       /* name */
     1,                     /* is_refcounted */
     0,                     /* template_count */
     0,                     /* flags */
     "Exception",           /* parent name */
     NULL,                  /* property seeds */
     NULL,                  /* setup_func */
     NULL,                  /* gc_marker */
     NULL},                 /* eq_func */

    {"ValueError",          /* name */
     1,                     /* is_refcounted */
     0,                     /* template_count */
     0,                     /* flags */
     "Exception",           /* parent name */
     NULL,                  /* property seeds */
     NULL,                  /* setup_func */
     NULL,                  /* gc_marker */
     NULL},                 /* eq_func */

    {"RecursionError",      /* name */
     1,                     /* is_refcounted */
     0,                     /* template_count */
     0,                     /* flags */
     "Exception",           /* parent name */
     NULL,                  /* property seeds */
     NULL,                  /* setup_func */
     NULL,                  /* gc_marker */
     NULL},                 /* eq_func */

    {"KeyError",            /* name */
     1,                     /* is_refcounted */
     0,                     /* template_count */
     0,                     /* flags */
     "Exception",           /* parent name */
     NULL,                  /* property seeds */
     NULL,                  /* setup_func */
     NULL,                  /* gc_marker */
     NULL},                 /* eq_func */

    {"FormatError",         /* name */
     1,                     /* is_refcounted */
     0,                     /* template_count */
     0,                     /* flags */
     "Exception",           /* parent name */
     NULL,                  /* property seeds */
     NULL,                  /* setup_func */
     NULL,                  /* gc_marker */
     NULL},                 /* eq_func */
=======
    {"integer",   0,  0,  CLS_VALID_HASH_KEY, NULL, NULL,               NULL,
     &lily_integer_eq},

    {"double",    0,  0,  CLS_VALID_HASH_KEY, NULL, NULL,               NULL,
     &lily_double_eq},

    {"string",    1,  0,  CLS_VALID_HASH_KEY, NULL, lily_string_setup,  NULL,
     &lily_string_eq},

    {"function",  0, -1,  0,                  NULL, NULL,               NULL,
     &lily_generic_eq},

    {"any",       1,  0,  0,                  NULL, NULL,               &lily_gc_any_marker,
     &lily_any_eq},

    {"list",      1,  1,  0,                  NULL, lily_list_setup,    &lily_gc_list_marker,
     &lily_list_eq},

    {"hash",      1,  2,  0,                  NULL, lily_hash_setup,    &lily_gc_hash_marker,
     &lily_hash_eq},

    {"tuple",     1, -1, 0,                   NULL, NULL,               &lily_gc_tuple_marker,
     &lily_tuple_eq},

    {"",          0,  0, 0,                   NULL, NULL,            NULL, NULL},
    {"package",   0,  0, 0,                   NULL, NULL,            NULL, NULL},
    {"Exception", 1,  0, 0,                   &traceback, NULL, NULL, NULL},
    {"DivisionByZeroError", 1,  0, 0,         &traceback, NULL, NULL, NULL}
>>>>>>> Stashed changes
};

typedef const struct {
    char *name;
    uint64_t shorthash;
} keyword_seed;

keyword_seed keywords[] = {
    {"if",           26217},
    {"elif",         1718185061},
    {"else",         1702063205},
    {"return",       121437875889522},
    {"while",        435610544247},
    {"continue",     7310870969309884259},
    {"break",        461195539042},
    {"show",         2003789939},
    {"__line__",     6872323081280184159},
    {"__file__",     6872323072689856351},
    {"__function__", 7598807797348065119},
    {"for",          7499622},
    {"do",           28516},
    {"isnil",        465625314153},
    {"try",          7959156},
    {"except",       128026086176869}
};

void lily_builtin_print(lily_vm_state *, lily_function_val *, uintptr_t *);
void lily_builtin_printfmt(lily_vm_state *, lily_function_val *, uintptr_t *);

static const lily_func_seed print =
    {"print", lily_builtin_print, NULL,
        {SYM_CLASS_FUNCTION, 2, 0, -1, SYM_CLASS_STRING}};
static const lily_func_seed printfmt =
    {"printfmt", lily_builtin_printfmt, &print,
        {SYM_CLASS_FUNCTION, 3, SIG_IS_VARARGS, -1, SYM_CLASS_STRING, SYM_CLASS_LIST, SYM_CLASS_ANY}};

/* This must always be set to the last func seed defined here. */
#define GLOBAL_SEED_START printfmt

#endif
