#ifndef LILY_SEED_SYMTAB_H
# define LILY_SEED_SYMTAB_H

# include "lily_cls_integer.h"
# include "lily_cls_double.h"
# include "lily_cls_bytestring.h"
# include "lily_cls_string.h"
# include "lily_cls_list.h"
# include "lily_cls_hash.h"
# include "lily_cls_file.h"
# include "lily_vm.h"
# include "lily_gc.h"
# include "lily_class_funcs.h"

/* Sync name order with SYM_CLASS_* #defines in lily_symtab.h */
typedef const struct {
    char *name;
    uint16_t is_refcounted;
    uint16_t generic_count;
    uint32_t flags;
    class_setup_func setup_func;
    gc_marker_func gc_marker;
    class_eq_func eq_func;
    class_destroy_func destroy_func;
} class_seed;

/* Note: If CLS_VALID_HASH_KEY is added to other classes, the vm will need to be
         updated to hash those classes right. It will also need KeyError
         printing to be touched up for that. Other things may also need updating
         too. */
class_seed class_seeds[] =
{
    {"integer",                 /* name */
     0,                         /* is_refcounted */
     0,                         /* generic_count */
     CLS_VALID_HASH_KEY,        /* flags */
     lily_integer_setup,        /* setup_func */
     NULL,                      /* gc_marker */
     &lily_integer_eq,          /* eq_func */
     NULL,                      /* destroy_func */
    },

    {"double",                  /* name */
     0,                         /* is_refcounted */
     0,                         /* generic_count */
     CLS_VALID_HASH_KEY,        /* flags */
     lily_double_setup,         /* setup_func */
     NULL,                      /* gc_marker */
     &lily_double_eq,           /* eq_func */
     NULL,                      /* destroy_func */
    },

    {"string",                  /* name */
     1,                         /* is_refcounted */
     0,                         /* generic_count */
     CLS_VALID_HASH_KEY,        /* flags */
     lily_string_setup,         /* setup_func */
     NULL,                      /* gc_marker */
     &lily_string_eq,           /* eq_func */
     lily_destroy_string        /* destroy_func */
    },

    {"bytestring",              /* name */
     1,                         /* is_refcounted */
     0,                         /* generic_count */
     0,                         /* flags */
     lily_bytestring_setup,     /* setup_func */
     NULL,                      /* gc_marker */
     &lily_bytestring_eq,       /* eq_func */
     lily_destroy_string        /* destroy_func */
    },

    {"symbol",                  /* name */
     1,                         /* is_refcounted */
     0,                         /* generic_count */
     CLS_VALID_HASH_KEY,        /* flags */
     NULL,                      /* setup_func */
     NULL,                      /* gc_marker */
     &lily_generic_eq,          /* eq_func */
     lily_destroy_symbol,       /* destroy_func */
    },

    {"function",                /* name */
     1,                         /* is_refcounted */
     -1,                        /* generic_count */
     0,                         /* flags */
     NULL,                      /* setup_func */
     NULL,                      /* gc_marker */
     &lily_generic_eq,          /* eq_func */
     lily_destroy_function      /* destroy_func */
    },

    {"any",                     /* name */
     1,                         /* is_refcounted */
     0,                         /* generic_count */
     /* 'any' is treated as an enum class that has all classes ever defined
        within it. */
     CLS_ENUM_CLASS,            /* flags */
     NULL,                      /* setup_func */
     &lily_gc_any_marker,       /* gc_marker */
     &lily_any_eq,              /* eq_func */
     lily_destroy_any           /* destroy_func */
    },

    {"list",                    /* name */
     1,                         /* is_refcounted */
     1,                         /* generic_count */
     0,                         /* flags */
     lily_list_setup,           /* setup_func */
     &lily_gc_list_marker,      /* gc_marker */
     &lily_list_eq,             /* eq_func */
     lily_destroy_list,         /* destroy_func */
    },

    {"hash",                    /* name */
     1,                         /* is_refcounted */
     2,                         /* generic_count */
     0,                         /* flags */
     lily_hash_setup,           /* setup_func */
     &lily_gc_hash_marker,      /* gc_marker */
     &lily_hash_eq,             /* eq_func */
     lily_destroy_hash,         /* destroy_func */
    },

    {"tuple",                   /* name */
     1,                         /* is_refcounted */
     -1,                        /* generic_count */
     0,                         /* flags */
     NULL,                      /* setup_func */
     &lily_gc_tuple_marker,     /* gc_marker */
     &lily_tuple_eq,            /* eq_func */
     lily_destroy_tuple         /* destroy_func */
    },

    {"file",                    /* name */
     1,                         /* is_refcounted */
     0,                         /* generic_count */
     0,                         /* flags */
     lily_file_setup,           /* setup_func */
     NULL,                      /* gc_marker */
     &lily_generic_eq,          /* eq_func */
     lily_destroy_file          /* destroy_func */
    },

    {"",                        /* name */
     0,                         /* is_refcounted */
     0,                         /* generic_count */
     0,                         /* flags */
     NULL,                      /* setup_func */
     NULL,                      /* gc_marker */
     NULL,                      /* eq_func */
     NULL,                      /* destroy_func */
    }
};

void lily_builtin_print(lily_vm_state *, lily_function_val *, uint16_t *);
void lily_builtin_show(lily_vm_state *, lily_function_val *, uint16_t *);
void lily_builtin_printfmt(lily_vm_state *, lily_function_val *, uint16_t *);
void lily_builtin_calltrace(lily_vm_state *, lily_function_val *, uint16_t *);

static const lily_func_seed calltrace =
    {"calltrace", "function calltrace( => list[tuple[string, integer]])", lily_builtin_calltrace, NULL};
static const lily_func_seed show =
    {"show", "function show[A](A)", lily_builtin_show, &calltrace};
static const lily_func_seed print =
    {"print", "function print(string)", lily_builtin_print, &show};
static const lily_func_seed printfmt =
    {"printfmt", "function printfmt(string, list[any]...)", lily_builtin_printfmt, &print};

/* This must always be set to the last func seed defined here. */
#define GLOBAL_SEED_START printfmt

#endif
