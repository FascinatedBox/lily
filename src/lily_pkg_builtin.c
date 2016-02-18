#include <stdio.h>
#include <string.h>

#include "lily_parser.h"
#include "lily_symtab.h"
#include "lily_value.h"
#include "lily_cls_integer.h"
#include "lily_cls_double.h"
#include "lily_cls_string.h"
#include "lily_cls_bytestring.h"
#include "lily_cls_boolean.h"
#include "lily_cls_function.h"
#include "lily_cls_dynamic.h"
#include "lily_cls_list.h"
#include "lily_cls_hash.h"
#include "lily_cls_file.h"
#include "lily_cls_option.h"
#include "lily_seed.h"

extern const lily_func_seed lily_option_dl_start;
extern const lily_func_seed lily_dynamic_dl_start;

static const lily_class_seed function_seed =
{
    NULL,                     /* next */
    "Function",               /* name */
    dyna_class,               /* load_type */
    1,                        /* is_refcounted */
    -1,                       /* generic_count */
    NULL,                     /* dynaload_table */
    lily_destroy_function     /* destroy_func */
};

static const lily_class_seed dynamic_seed =
{
    NULL,                     /* next */
    "Dynamic",                /* name */
    dyna_class,               /* load_type */
    1,                        /* is_refcounted */
    0,                        /* generic_count */
    &lily_dynamic_dl_start,   /* dynaload_table */
    lily_destroy_dynamic      /* destroy_func */
};

static const lily_class_seed tuple_seed =
{
    NULL,                     /* next */
    "Tuple",                  /* name */
    dyna_class,               /* load_type */
    1,                        /* is_refcounted */
    -1,                       /* generic_count */
    NULL,                     /* dynaload_table */
    NULL                      /* destroy_func */
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
    NULL,                     /* dynaload_table */
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
    NULL,                     /* dynaload_table */
    NULL                      /* destroy_func */
};

static const lily_class_seed question_seed =
    /* This class is used as a placeholder when a full type isn't yet known.
       No instances of this fake class are ever created. */
{
    NULL,                     /* next */
    "?",                      /* name */
    dyna_class,               /* load_type */
    0,                        /* is_refcounted */
    0,                        /* generic_count */
    NULL,                     /* dynaload_table */
    NULL                      /* destroy_func */
};

void lily_builtin_print(lily_vm_state *, uint16_t, uint16_t *);
void lily_builtin_printfmt(lily_vm_state *, uint16_t, uint16_t *);
void lily_builtin_calltrace(lily_vm_state *, uint16_t, uint16_t *);

static const lily_variant_seed seed_none =
{
    NULL,
    "None",
    dyna_variant,
    "",
    "Option"
};

static const lily_variant_seed seed_some =
{
    &seed_none,
    "Some",
    dyna_variant,
    "(A)",
    "Option"
};

static const lily_enum_seed seed_option =
{
    &seed_some,
    "Option",
    dyna_builtin_enum,
    1,
    SYM_CLASS_OPTION,
    &lily_option_dl_start,
};

static const lily_base_seed io_error =
    {&seed_option, "IOError", dyna_exception};
static const lily_base_seed format_error =
    {&io_error, "FormatError", dyna_exception};
static const lily_base_seed key_error =
    {&format_error, "KeyError", dyna_exception};
static const lily_base_seed runtime_error =
    {&key_error, "RuntimeError", dyna_exception};
static const lily_base_seed value_error =
    {&runtime_error, "ValueError", dyna_exception};
static const lily_base_seed index_error =
    {&value_error, "IndexError", dyna_exception};
static const lily_base_seed dbz_error =
    {&index_error, "DivisionByZeroError", dyna_exception};
static const lily_func_seed calltrace =
    {&dbz_error, "calltrace", dyna_function, ":List[String]", lily_builtin_calltrace};
static const lily_func_seed print =
    {&calltrace, "print", dyna_function, "[A](A)", lily_builtin_print};
static const lily_func_seed printfmt =
    {&print, "printfmt", dyna_function, "(String, Dynamic...)", lily_builtin_printfmt};
static const lily_var_seed seed_stderr =
        {&printfmt, "stderr", dyna_var, "File"};
static const lily_var_seed seed_stdout =
        {&seed_stderr, "stdout", dyna_var, "File"};
static const lily_var_seed seed_stdin =
        {&seed_stdout, "stdin", dyna_var, "File"};

static void builtin_var_loader(lily_parse_state *parser, lily_var *var)
{
    char *name = var->name;
    FILE *source;
    char mode;

    if (strcmp(name, "stdin") == 0) {
        source = stdin;
        mode = 'r';
    }
    else if (strcmp(name, "stdout") == 0) {
        source = stdout;
        mode = 'w';
    }
    else {
        source = stderr;
        mode = 'w';
    }

    lily_file_val *file_val = lily_new_file_val(source, mode);
    /* This prevents std* streams from being closed. */
    file_val->is_builtin = 1;

    lily_value v;
    v.flags = 0;
    v.value = (lily_raw_value)file_val;

    lily_tie_value(parser->symtab, var, &v);
}

void lily_init_builtin_package(lily_symtab *symtab, lily_import_entry *builtin)
{
    symtab->integer_class    = lily_integer_init(symtab);
    symtab->double_class     = lily_double_init(symtab);
    symtab->string_class     = lily_string_init(symtab);
    symtab->bytestring_class = lily_bytestring_init(symtab);
    symtab->boolean_class    = lily_boolean_init(symtab);
    symtab->function_class   = lily_new_class_by_seed(symtab, &function_seed);
    symtab->dynamic_class    = lily_new_class_by_seed(symtab, &dynamic_seed);
    symtab->list_class       = lily_list_init(symtab);
    symtab->hash_class       = lily_hash_init(symtab);
    symtab->tuple_class      = lily_new_class_by_seed(symtab, &tuple_seed);
    symtab->optarg_class     = lily_new_class_by_seed(symtab, &optarg_seed);
    lily_class *file_class   = lily_file_init(symtab);
    symtab->generic_class    = lily_new_class_by_seed(symtab, &generic_seed);
    symtab->question_class   = lily_new_class_by_seed(symtab, &question_seed);

    symtab->integer_class->flags    |= CLS_VALID_OPTARG | CLS_VALID_HASH_KEY;
    symtab->double_class->flags     |= CLS_VALID_OPTARG;
    symtab->string_class->flags     |= CLS_VALID_OPTARG | CLS_VALID_HASH_KEY;
    symtab->bytestring_class->flags |= CLS_VALID_OPTARG;
    symtab->boolean_class->flags    |= CLS_VALID_OPTARG;

    symtab->integer_class->move_flags    = VAL_IS_INTEGER;
    symtab->double_class->move_flags     = VAL_IS_DOUBLE;
    symtab->string_class->move_flags     = VAL_IS_STRING;
    symtab->bytestring_class->move_flags = VAL_IS_BYTESTRING;
    symtab->boolean_class->move_flags    = VAL_IS_BOOLEAN;
    symtab->function_class->move_flags   = VAL_IS_FUNCTION;
    symtab->dynamic_class->move_flags    = VAL_IS_DYNAMIC;
    symtab->list_class->move_flags       = VAL_IS_LIST;
    symtab->hash_class->move_flags       = VAL_IS_HASH;
    symtab->tuple_class->move_flags      = VAL_IS_TUPLE;
    file_class->move_flags               = VAL_IS_FILE;

    /* These need to be set here so type finalization can bubble them up. */
    symtab->generic_class->type->flags |= TYPE_IS_UNRESOLVED;
    symtab->question_class->type->flags |= TYPE_IS_INCOMPLETE;

    /* HACK: This ensures that there is a 'hole' for Option to dynaload into. */
    symtab->next_class_id = START_CLASS_ID;

    builtin->dynaload_table = &seed_stdin;
    builtin->var_load_fn = builtin_var_loader;
}
