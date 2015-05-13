#include <stdio.h>

#include "lily_parser.h"
#include "lily_symtab.h"
#include "lily_value.h"
#include "lily_cls_integer.h"
#include "lily_cls_double.h"
#include "lily_cls_string.h"
#include "lily_cls_bytestring.h"
#include "lily_cls_symbol.h"
#include "lily_cls_function.h"
#include "lily_cls_any.h"
#include "lily_cls_list.h"
#include "lily_cls_hash.h"
#include "lily_cls_tuple.h"
#include "lily_cls_file.h"
#include "lily_seed.h"

static const lily_class_seed class_seeds[] =
{
    {NULL,                      /* next */
     "integer",                 /* name */
     dyna_class,                /* load_type */
     0,                         /* is_refcounted */
     0,                         /* generic_count */
     CLS_VALID_HASH_KEY,        /* flags */
     lily_integer_setup,        /* setup_func */
     NULL,                      /* gc_marker */
     &lily_integer_eq,          /* eq_func */
     NULL,                      /* destroy_func */
    },

    {NULL,                      /* next */
     "double",                  /* name */
     dyna_class,                /* load_type */
     0,                         /* is_refcounted */
     0,                         /* generic_count */
     CLS_VALID_HASH_KEY,        /* flags */
     lily_double_setup,         /* setup_func */
     NULL,                      /* gc_marker */
     &lily_double_eq,           /* eq_func */
     NULL,                      /* destroy_func */
    },

    {NULL,                      /* next */
     "string",                  /* name */
     dyna_class,                /* load_type */
     1,                         /* is_refcounted */
     0,                         /* generic_count */
     CLS_VALID_HASH_KEY,        /* flags */
     lily_string_setup,         /* setup_func */
     NULL,                      /* gc_marker */
     &lily_string_eq,           /* eq_func */
     lily_destroy_string        /* destroy_func */
    },

    {NULL,                      /* next */
     "bytestring",              /* name */
     dyna_class,                /* load_type */
     1,                         /* is_refcounted */
     0,                         /* generic_count */
     0,                         /* flags */
     lily_bytestring_setup,     /* setup_func */
     NULL,                      /* gc_marker */
     &lily_bytestring_eq,       /* eq_func */
     lily_destroy_string        /* destroy_func */
    },

    {NULL,                      /* next */
     "symbol",                  /* name */
     dyna_class,                /* load_type */
     1,                         /* is_refcounted */
     0,                         /* generic_count */
     CLS_VALID_HASH_KEY,        /* flags */
     NULL,                      /* setup_func */
     NULL,                      /* gc_marker */
     &lily_generic_eq,          /* eq_func */
     lily_destroy_symbol,       /* destroy_func */
    },

    {NULL,                      /* next */
     "function",                /* name */
     dyna_class,                /* load_type */
     1,                         /* is_refcounted */
     -1,                        /* generic_count */
     0,                         /* flags */
     NULL,                      /* setup_func */
     NULL,                      /* gc_marker */
     &lily_generic_eq,          /* eq_func */
     lily_destroy_function      /* destroy_func */
    },

    {NULL,                      /* next */
     "any",                     /* name */
     dyna_class,                /* load_type */
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

    {NULL,                      /* next */
     "list",                    /* name */
     dyna_class,                /* load_type */
     1,                         /* is_refcounted */
     1,                         /* generic_count */
     0,                         /* flags */
     lily_list_setup,           /* setup_func */
     &lily_gc_list_marker,      /* gc_marker */
     &lily_list_eq,             /* eq_func */
     lily_destroy_list,         /* destroy_func */
    },

    {NULL,                      /* next */
     "hash",                    /* name */
     dyna_class,                /* load_type */
     1,                         /* is_refcounted */
     2,                         /* generic_count */
     0,                         /* flags */
     lily_hash_setup,           /* setup_func */
     &lily_gc_hash_marker,      /* gc_marker */
     &lily_hash_eq,             /* eq_func */
     lily_destroy_hash,         /* destroy_func */
    },

    {NULL,                      /* next */
     "tuple",                   /* name */
     dyna_class,                /* load_type */
     1,                         /* is_refcounted */
     -1,                        /* generic_count */
     0,                         /* flags */
     NULL,                      /* setup_func */
     &lily_gc_tuple_marker,     /* gc_marker */
     &lily_tuple_eq,            /* eq_func */
     lily_destroy_tuple         /* destroy_func */
    },

    /* This is the optarg class. The type inside of it is what may/may not be
       sent. */
    {NULL,                      /* next */
     "*",                       /* name */
     dyna_class,                /* load_type */
     0,                         /* is_refcounted */
     1,                         /* generic_count */
     0,                         /* flags */
     NULL,                      /* setup_func */
     NULL,                      /* gc_marker */
     NULL,                      /* eq_func */
     NULL                       /* destroy_func */
    },

    {NULL,                      /* next */
     "file",                    /* name */
     dyna_class,                /* load_type */
     1,                         /* is_refcounted */
     0,                         /* generic_count */
     0,                         /* flags */
     lily_file_setup,           /* setup_func */
     NULL,                      /* gc_marker */
     &lily_generic_eq,          /* eq_func */
     lily_destroy_file          /* destroy_func */
    },

    /* This is the generic class. Types of this class are created and have a
       generic_pos set to indicate what letter they are (A = 0, B = 1, etc.) */
    {NULL,                      /* next */
     "",                        /* name */
     dyna_class,                /* load_type */
     0,                         /* is_refcounted */
     0,                         /* generic_count */
     0,                         /* flags */
     NULL,                      /* setup_func */
     NULL,                      /* gc_marker */
     NULL,                      /* eq_func */
     NULL                       /* destroy_func */
    },
};

void lily_builtin_print(lily_vm_state *, lily_function_val *, uint16_t *);
void lily_builtin_show(lily_vm_state *, lily_function_val *, uint16_t *);
void lily_builtin_printfmt(lily_vm_state *, lily_function_val *, uint16_t *);
void lily_builtin_calltrace(lily_vm_state *, lily_function_val *, uint16_t *);

static const lily_func_seed calltrace =
    {NULL, "calltrace", dyna_function, "function calltrace( => list[tuple[string, string, integer]])", lily_builtin_calltrace};
static const lily_func_seed show =
    {&calltrace, "show", dyna_function, "function show[A](A)", lily_builtin_show};
static const lily_func_seed print =
    {&show, "print", dyna_function, "function print(string)", lily_builtin_print};
static const lily_func_seed printfmt =
    {&print, "printfmt", dyna_function, "function printfmt(string, list[any]...)", lily_builtin_printfmt};
static const lily_base_seed seed_stderr = {&printfmt, "stderr", dyna_var};
static const lily_base_seed seed_stdout = {&seed_stderr, "stdout", dyna_var};
static const lily_base_seed seed_stdin = {&seed_stdout, "stdin", dyna_var};

static lily_var *builtin_var_loader(lily_parse_state *parser, const char *name)
{
    FILE *source;
    char mode;
    if (strcmp(name, "stdin") == 0) {
        source = stdin;
        mode = 'w';
    }
    else if (strcmp(name, "stdout") == 0) {
        source = stdout;
        mode = 'r';
    }
    else {
        source = stderr;
        mode = 'w';
    }

    lily_type *file_type = lily_type_by_name(parser, "file");
    lily_raw_value raw = {.file = lily_new_file_val(source, mode)};
    return lily_new_dynaload_var(parser->symtab, file_type, name, raw);
}

void lily_init_builtin_package(lily_symtab *symtab, lily_import_entry *builtin)
{
    symtab->integer_class    = lily_new_class_by_seed(symtab, &class_seeds[0]);
    symtab->double_class     = lily_new_class_by_seed(symtab, &class_seeds[1]);
    symtab->string_class     = lily_new_class_by_seed(symtab, &class_seeds[2]);
    symtab->bytestring_class = lily_new_class_by_seed(symtab, &class_seeds[3]);
    symtab->symbol_class     = lily_new_class_by_seed(symtab, &class_seeds[4]);
    symtab->function_class   = lily_new_class_by_seed(symtab, &class_seeds[5]);
    symtab->any_class        = lily_new_class_by_seed(symtab, &class_seeds[6]);
    symtab->list_class       = lily_new_class_by_seed(symtab, &class_seeds[7]);
    symtab->hash_class       = lily_new_class_by_seed(symtab, &class_seeds[8]);
    symtab->tuple_class      = lily_new_class_by_seed(symtab, &class_seeds[9]);
    symtab->optarg_class     = lily_new_class_by_seed(symtab, &class_seeds[10]);
    lily_new_class_by_seed(symtab, &class_seeds[11]);
    symtab->generic_class    = lily_new_class_by_seed(symtab, &class_seeds[12]);

    symtab->any_class->type->flags |= TYPE_MAYBE_CIRCULAR;
    symtab->generic_type_start = symtab->generic_class->type;
    symtab->next_class_id = 13;

    builtin->dynaload_table = &seed_stdin;
    builtin->var_load_fn = builtin_var_loader;
    builtin->class_chain = symtab->class_chain;
}
