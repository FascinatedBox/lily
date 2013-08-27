#ifndef LILY_SYMINFO_H
# define LILY_SYMINFO_H

# include <stdint.h>

/* lily_syminfo.h is included by a lot of things, because it defined lily_value
   and lily_sig, which are core to lily. Functions get the vm state, so that
   they can raise proper lily errors. The vm state is not filled in here because
   few modules will want to touch the func of a value. */
struct lily_vm_state_t;
struct lily_sym_t;

typedef void (*lily_func)(struct lily_vm_state_t *, int, struct lily_sym_t **);

/* Here are the current flag values. The first three are only to be set on
   symbols in the symtab. This allows lily_debug to give better debug info, and
   a few other nifty tricks. */
#define VAR_SYM       0x01
#define LITERAL_SYM   0x02
#define STORAGE_SYM   0x04
/* If a symbol doesn't have a value, then the symbol's flags are set to S_IS_NIL
   to indicate such. Lists contain an array of flags for each of their symbols.
   S_IS_NIL is set if a particular position in a list is nil. However, lists do
   not use any of the above flags, because only values are stored in lists. */
#define S_IS_NIL      0x10
/* This flag is set if the object points to a higher point in the list, usually
   the top of the list itself. This is used to prevent objects containing
   higher-level elements from deref-ing them. */
#define S_IS_CIRCULAR 0x20

/* This is where the raw data gets stored. Anything not an integer or a number
   is stored in ptr and cast appropriately. */
typedef union {
    int64_t integer;
    double number;
    struct lily_str_val_t *str;
    struct lily_method_val_t *method;
    struct lily_object_val_t *object;
    struct lily_list_val_t *list;
    struct lily_generic_val_t *generic;
    struct lily_function_val_t *function;
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
    /* This is here so trace can print the actual name of the method being
       called. This is necessary because of indirect and anonymous calls. This
       is a copy of the var's data, so don't free it. */
    char *trace_name;
} lily_method_val;

typedef struct lily_object_val_t {
    int refcount;
    lily_value value;
    struct lily_sig_t *sig;
} lily_object_val;

typedef struct lily_list_val_t {
    int refcount;
    lily_value *values;
    int *flags;
    struct lily_list_val_t *parent;
    /* This is used by circular reference checking to keep from going into an
       infinite loop. */
    int visited;
    int num_values;
} lily_list_val;

typedef struct lily_function_val_t {
    int refcount;
    lily_func func;
    char *trace_name;
} lily_function_val;

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
    union {
        lily_call_sig *call;
        struct lily_sig_t *value_sig;
    } node;
    struct lily_sig_t *next;
} lily_sig;

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
#define INITIAL_CLASS_SIZE 8

#endif
