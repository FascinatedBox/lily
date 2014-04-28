#ifndef LILY_SYMINFO_H
# define LILY_SYMINFO_H

# include <stdint.h>

/* lily_syminfo.h is included by a lot of things, because it defined lily_value
   and lily_sig, which are core to lily. Functions get the vm state, so that
   they can raise proper lily errors. The vm state is not filled in here because
   few modules will want to touch the func of a value. */
struct lily_vm_state_t;
struct lily_sym_t;
struct lily_method_info_t;
struct lily_vm_register_t;
struct lily_sig_t;

typedef void (*lily_func)(struct lily_vm_state_t *, uintptr_t *code, int);

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
    struct lily_generic_gc_val_t *gc_generic;
    struct lily_function_val_t *function;
    struct lily_hash_val_t *hash;
} lily_value;

typedef void (*gc_marker_func)(int, struct lily_sig_t *, lily_value);

typedef struct lily_str_val_t {
    int refcount;
    char *str;
    int size;
} lily_str_val;

typedef struct lily_method_val_t {
    int refcount;
    uintptr_t *code;

    struct lily_register_info_t *reg_info;
    int reg_count;
    int pos;
    int len;
    /* This is here so trace can print the actual name of the method being
       called. This is necessary because of indirect and anonymous calls. This
       is a copy of the var's data, so don't free it. */
    char *trace_name;
} lily_method_val;

typedef struct lily_object_val_t {
    int refcount;
    /* Objects always have a gc_entry, because they can easily circularly
       reference. */
    struct lily_gc_entry_t *gc_entry;
    lily_value value;
    struct lily_sig_t *sig;
} lily_object_val;

typedef struct lily_gc_entry_t {
    struct lily_sig_t *value_sig;
    lily_value value;
    int last_pass;
    struct lily_gc_entry_t *next;
} lily_gc_entry;

typedef struct lily_list_val_t {
    int refcount;
    /* Lists have a gc_entry ONLY if their signature says they can be circular.
       A list can currently only be circular if it contains objects at some
       point. This avoids creating gc entries for, say, list[integer] which can
       be collected via refcounting. */
    struct lily_gc_entry_t *gc_entry;
    lily_value *values;
    int *flags;
    int visited;
    int num_values;
} lily_list_val;

typedef struct lily_function_val_t {
    int refcount;
    lily_func func;
    char *trace_name;
} lily_function_val;

typedef struct lily_hash_elem_t {
    int flags;
    lily_value key;
    uint64_t key_siphash;
    lily_value value;
    struct lily_hash_elem_t *next;
} lily_hash_elem;

typedef struct lily_hash_val_t {
    int refcount;
    int visited;
    int num_elems;
    lily_hash_elem *elem_chain;
} lily_hash_val;

/* Every ref'd value is a superset of the 'generic' value (refcount
   comes first). This allows the vm to make refs/derefs a bit easier. */
typedef struct lily_generic_val_t {
    int refcount;
} lily_generic_val;

/* Every value that can be garbage collected is a superset of this type. This
   allows using the gc_entry field without having to worry about which type is
   being used. */
typedef struct lily_generic_gc_val_t {
    int refcount;
    lily_gc_entry *gc_entry;
} lily_generic_gc_val;

/* Indicates what kind of value is being stored. */
typedef struct lily_class_t {
    char *name;
    uint64_t shorthash;
    int id;
    int is_refcounted;
    int template_count;
    struct lily_sig_t *sig;
    struct lily_var_t *call_start;
    struct lily_var_t *call_top;

    /* This is an internal function that dives into a value and marks the value
       and anything inside of the value as being visited. This lets the gc know
       what values are visible by marking them as visited, hence the name. */
    gc_marker_func gc_marker;
} lily_class;

/* If set, the signature is either a vararg method or function. The last
   argument is the type for varargs. */
#define SIG_IS_VARARGS     0x1
/* If this is set, a gc entry is allocated for the type. This means that the
   value is a superset of lily_generic_gc_val_t. */
#define SIG_MAYBE_CIRCULAR 0x2

typedef struct lily_sig_t {
    lily_class *cls;
    int template_pos;

    struct lily_sig_t **siglist;
    int siglist_size;
    int flags;

    struct lily_sig_t *next;
} lily_sig;

/* This is used to initialize the registers that a method uses. */
typedef struct lily_register_info_t {
    lily_sig *sig;
    char *class_name;
    char *name;
    int line_num;
} lily_register_info;

#define SYM_SCOPE_GLOBAL       0x01
#define SYM_TYPE_LITERAL       0x02
#define SYM_TYPE_VAR           0x04
#define SYM_TYPE_STORAGE       0x10
/* If a symbol doesn't have a value, then the symbol's flags are set to S_IS_NIL
   to indicate such. Lists contain an array of flags for each of their symbols.
   S_IS_NIL is set if a particular position in a list is nil. However, lists do
   not use any of the above flags, because only values are stored in lists. */
#define SYM_IS_NIL             0x20
/* This var is out of scope. This is set when a var in a non-method block goes
   out of scope. */
#define SYM_OUT_OF_SCOPE       0x40

/* Registers are allocated to hold values for calls. Opcodes reference registers
   instead of specific addresses. */
typedef struct lily_vm_register_t {
    int flags;
    lily_sig *sig;
    lily_value value;
} lily_vm_register;

typedef struct lily_storage_t {
    int flags;
    lily_sig *sig;
    lily_value unused;
    int reg_spot;
    int expr_num;
    struct lily_storage_t *next;
} lily_storage;

typedef struct lily_var_t {
    int flags;
    lily_sig *sig;
    lily_value value;
    int reg_spot;
    char *name;
    uint64_t shorthash;
    int line_num;
    /* This is used to make sure that methods can't access vars out of their
       current depth (or at the global level). */
    /* todo: A better way to do this would be to scope out values that should
             not be accessed. */
    int method_depth;
    struct lily_var_t *next;
} lily_var;

typedef struct lily_sym_t {
    int flags;
    lily_sig *sig;
    lily_value value;
    int reg_spot;
} lily_sym;

/* Literals are syms created to hold values that the lexer finds. These syms
   always have a value set. */
typedef struct lily_literal_t {
    int flags;
    lily_sig *sig;
    lily_value value;
    int reg_spot;
    struct lily_literal_t *next;
} lily_literal;

/* Sync with classname_seeds in lily_seed_symtab.h. */
#define SYM_CLASS_INTEGER  0
#define SYM_CLASS_NUMBER   1
#define SYM_CLASS_STR      2
#define SYM_CLASS_FUNCTION 3
#define SYM_CLASS_OBJECT   4
#define SYM_CLASS_METHOD   5
#define SYM_CLASS_LIST     6
#define SYM_CLASS_HASH     7
#define SYM_CLASS_TEMPLATE 8

#define SYM_LAST_CLASS     8
#define INITIAL_CLASS_SIZE 9

#endif
