#include <stdio.h>
#include <string.h>

#include "lily_parser.h"
#include "lily_symtab.h"
#include "lily_value.h"
#include "lily_cls_integer.h"
#include "lily_cls_double.h"
#include "lily_cls_string.h"
#include "lily_cls_bytestring.h"
#include "lily_cls_function.h"
#include "lily_cls_any.h"
#include "lily_cls_list.h"
#include "lily_cls_hash.h"
#include "lily_cls_tuple.h"
#include "lily_cls_file.h"
#include "lily_seed.h"

static const lily_class_seed function_seed =
{
    NULL,                     /* next */
    "function",               /* name */
    dyna_class,               /* load_type */
    1,                        /* is_refcounted */
    -1,                       /* generic_count */
    0,                        /* flags */
    NULL,                     /* dynaload_table */
    &lily_gc_function_marker, /* gc_marker */
    &lily_generic_eq,         /* eq_func */
    lily_destroy_function     /* destroy_func */
};

static const lily_class_seed any_seed =
{
    NULL,                     /* next */
    "any",                    /* name */
    dyna_class,               /* load_type */
    1,                        /* is_refcounted */
    0,                        /* generic_count */
    /* 'any' is treated as an enum class that has all classes ever defined
       within it. */
    CLS_ENUM_CLASS,           /* flags */
    NULL,                     /* dynaload_table */
    &lily_gc_any_marker,      /* gc_marker */
    &lily_any_eq,             /* eq_func */
    lily_destroy_any          /* destroy_func */
};

static const lily_class_seed tuple_seed =
{
    NULL,                     /* next */
    "tuple",                  /* name */
    dyna_class,               /* load_type */
    1,                        /* is_refcounted */
    -1,                       /* generic_count */
    0,                        /* flags */
    NULL,                     /* dynaload_table */
    &lily_gc_tuple_marker,    /* gc_marker */
    &lily_tuple_eq,           /* eq_func */
    lily_destroy_tuple        /* destroy_func */
};

static const lily_class_seed optarg_seed =
    /* This is the optarg class. The type inside of it is what may/may not be
       sent. */
{
    NULL,                     /* next */
    "*",                      /* name */
    dyna_class,               /* load_type */
    0,                        /* is_refcounted */
    1,                        /* generic_count */
    0,                        /* flags */
    NULL,                     /* dynaload_table */
    NULL,                     /* gc_marker */
    NULL,                     /* eq_func */
    NULL                      /* destroy_func */
};

static const lily_class_seed generic_seed =
    /* This is the generic class. Types of this class are created and have a
       generic_pos set to indicate what letter they are (A = 0, B = 1, etc.) */
{
    NULL,                     /* next */
    "",                       /* name */
    dyna_class,               /* load_type */
    0,                        /* is_refcounted */
    0,                        /* generic_count */
    0,                        /* flags */
    NULL,                     /* dynaload_table */
    NULL,                     /* gc_marker */
    NULL,                     /* eq_func */
    NULL                      /* destroy_func */
};

void lily_builtin_print(lily_vm_state *, uint16_t, uint16_t *);
void lily_builtin_show(lily_vm_state *, uint16_t, uint16_t *);
void lily_builtin_printfmt(lily_vm_state *, uint16_t, uint16_t *);
void lily_builtin_calltrace(lily_vm_state *, uint16_t, uint16_t *);

static const lily_base_seed io_error =
    {NULL, "IOError", dyna_exception};
static const lily_base_seed format_error =
    {&io_error, "FormatError", dyna_exception};
static const lily_base_seed key_error =
    {&format_error, "KeyError", dyna_exception};
static const lily_base_seed recursion_error =
    {&key_error, "RecursionError", dyna_exception};
static const lily_base_seed value_error =
    {&recursion_error, "ValueError", dyna_exception};
static const lily_base_seed bad_tc_error =
    {&value_error, "BadTypecastError", dyna_exception};
static const lily_base_seed index_error =
    {&bad_tc_error, "IndexError", dyna_exception};
static const lily_base_seed dbz_error =
    {&index_error, "DivisionByZeroError", dyna_exception};
static const lily_func_seed calltrace =
    {&dbz_error, "calltrace", dyna_function, "function calltrace( => list[tuple[string, string, integer]])", lily_builtin_calltrace};
static const lily_func_seed show =
    {&calltrace, "show", dyna_function, "function show[A](A)", lily_builtin_show};
static const lily_func_seed print =
    {&show, "print", dyna_function, "function print(string)", lily_builtin_print};
static const lily_func_seed printfmt =
    {&print, "printfmt", dyna_function, "function printfmt(string, list[any]...)", lily_builtin_printfmt};
static const lily_var_seed seed_stderr =
        {&printfmt, "stderr", dyna_var, "file"};
static const lily_var_seed seed_stdout =
        {&seed_stderr, "stdout", dyna_var, "file"};
static const lily_var_seed seed_stdin =
        {&seed_stdout, "stdin", dyna_var, "file"};

static void builtin_var_loader(lily_parse_state *parser, lily_var *var)
{
    char *name = var->name;
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

    lily_raw_value raw = {.file = lily_new_file_val(source, mode)};
    lily_value v;
    v.flags = 0;
    v.type = var->type;
    v.value = raw;

    lily_tie_value(parser->symtab, var, &v);
}

void lily_init_builtin_package(lily_symtab *symtab, lily_import_entry *builtin)
{
    symtab->integer_class    = lily_integer_init(symtab);
    symtab->double_class     = lily_double_init(symtab);
    symtab->string_class     = lily_string_init(symtab);
    symtab->bytestring_class = lily_bytestring_init(symtab);
    symtab->function_class   = lily_new_class_by_seed(symtab, &function_seed);
    symtab->any_class        = lily_new_class_by_seed(symtab, &any_seed);
    symtab->list_class       = lily_list_init(symtab);
    symtab->hash_class       = lily_hash_init(symtab);
    symtab->tuple_class      = lily_new_class_by_seed(symtab, &tuple_seed);
    symtab->optarg_class     = lily_new_class_by_seed(symtab, &optarg_seed);
    lily_file_init(symtab);
    symtab->generic_class    = lily_new_class_by_seed(symtab, &generic_seed);

    symtab->any_class->type->flags |= TYPE_MAYBE_CIRCULAR;
    /* This needs to be set because symtab's type finalize bubbles this
       unresolved flag upward. */
    symtab->generic_class->type->flags |= TYPE_IS_UNRESOLVED;
    symtab->generic_type_start = symtab->generic_class->type;
    symtab->next_class_id = 12;

    builtin->dynaload_table = &seed_stdin;
    builtin->var_load_fn = builtin_var_loader;
}
