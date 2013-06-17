#ifndef LILY_SYMINFO_H
# define LILY_SYMINFO_H

# include <stdint.h>

/* This is where the raw data gets stored. Anything not an integer or a number
   is stored in ptr and cast appropriately. */
typedef union {
    int integer;
    double number;
    struct lily_str_val_t *str;
    struct lily_method_val_t *method;
    struct lily_object_val_t *object;
    struct lily_list_val_t *list;
    struct lily_generic_val_t *generic;
    void *ptr;
} lily_value;

typedef struct lily_str_val_t {
    int refcount;
    char *str;
    int size;
} lily_str_val;

typedef struct lily_method_val_t {
    int refcount;
    uintptr_t *code;
    struct lily_var_t *first_arg;
    struct lily_var_t *last_arg;
    int pos;
    int len;
} lily_method_val;

typedef struct lily_object_val_t {
    int refcount;
    lily_value value;
    struct lily_sig_t *sig;
} lily_object_val;

typedef struct lily_list_val_t {
    int refcount;
    lily_value *values;
    unsigned char *val_is_nil;
    int num_values;
} lily_list_val;

/* Every ref'd value is a superset of the 'generic' value (refcount
   comes first). This allows the vm to make refs/derefs a bit easier. */
typedef struct lily_generic_val_t {
    int refcount;
} lily_generic_val;

/* Indicates what kind of value is being stored. */
typedef struct lily_class_t {
    char *name;
    int id;
    int is_refcounted;
    struct lily_storage_t *storage;
    struct lily_sig_t *sig;
    struct lily_var_t *call_start;
    struct lily_var_t *call_top;
} lily_class;

typedef struct lily_call_sig_t {
    struct lily_sig_t *ret;
    struct lily_sig_t **args;
    int num_args;
    int is_varargs;
} lily_call_sig;

typedef struct lily_sig_t {
    lily_class *cls;
    /* This refcount was created so that complex list signatures could
       be shared without worry of the inner signature being deleted.
       Refcount is incremented even for basic signatures. */
    int refcount;
    union {
        lily_call_sig *call;
        struct lily_sig_t *value_sig;
    } node;
} lily_sig;

#define VAR_SYM      0x01
#define LITERAL_SYM  0x02
#define STORAGE_SYM  0x04
#define S_IS_NIL     0x10

/* All symbols have at least these fields. The vm and debugging functions use
   this to cast and grab common info. */
typedef struct lily_sym_t {
    int id;
    int flags;
    lily_sig *sig;
    lily_value value;
} lily_sym;

/* Literals are syms created to hold values that the lexer finds. These syms
   always have a value set. */
typedef struct lily_literal_t {
    int id;
    int flags;
    lily_sig *sig;
    lily_value value;
    struct lily_literal_t *next;
} lily_literal;

/* These are created by keywords (initialization), or by user declaration. They
   always have a class, but don't have a value at parse-time. Builtin symbols
   have line_num == 0. */
typedef struct lily_var_t {
    int id;
    int flags;
    lily_sig *sig;
    lily_value value;
    char *name;
    int line_num;
    lily_class *parent;
    struct lily_var_t *next;
} lily_var;

/* These hold the results of a vm op. An usage example would be holding the
   result of a plus operation. Each expression has a different id (expr_num) to
   keep the same storage from being used twice in an expression. Storages are
   circularly-linked, and held in the class that they are of (integer storages
   are in the integer class). */
typedef struct lily_storage_t {
    int id;
    int flags;
    lily_sig *sig;
    lily_value value;
    int expr_num;
    struct lily_storage_t *next;
} lily_storage;

/* Sync with classname_seeds in lily_seed_symtab.h. */
#define SYM_CLASS_INTEGER  0
#define SYM_CLASS_NUMBER   1
#define SYM_CLASS_STR      2
#define SYM_CLASS_FUNCTION 3
#define SYM_CLASS_OBJECT   4
#define SYM_CLASS_METHOD   5
#define SYM_CLASS_LIST     6

#define SYM_LAST_CLASS     6

#endif
