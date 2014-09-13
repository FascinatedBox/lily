#ifndef LILY_SYMINFO_H
# define LILY_SYMINFO_H

# include <stdint.h>

/* lily_syminfo.h defines all core types used by the language. Many of these
   types reference each other. Some are left incomplete, because not every
   part of the interpreter uses all of them. */

struct lily_class_t;
struct lily_vm_state_t;
struct lily_value_t;
struct lily_var_t;
struct lily_sig_t;
struct lily_register_info_t;
struct lily_func_seed_t;
struct lily_function_val_t;

/* gc_marker_func is a function called to mark all values within a given value.
   The is used by the gc to mark values as being visited. Values not visited
   will be collected. */
typedef void (*gc_marker_func)(int, struct lily_value_t *);
/* Lily's foreign functions look like this. */
typedef void (*lily_foreign_func)(struct lily_vm_state_t *, struct lily_function_val_t *,
        uintptr_t *);
/* This is called to set the seed_table of a class to a something non-NULL. It
   can also do other setup if the class wants to. This is called after all
   classes have been created.
   Returns 1 if successful, 0 otherwise. */
typedef int (*class_setup_func)(struct lily_class_t *);
/* This is called to do == and != when the vm has complex values, and also for
   comparing values held in an any. The vm is passed as a guard against
   an infinite loop. */
typedef int (*class_eq_func)(struct lily_vm_state_t *, int *,
        struct lily_value_t *, struct lily_value_t *);

/* lily_raw_value is a union of all possible values, plus a bit more. This is
   not common, because lily_value (which has flags and a sig) is typically
   used for parameters and such instead. However, this does have some uses. */
typedef union lily_raw_value_t {
    int64_t integer;
    double doubleval;
    struct lily_string_val_t *string;
    struct lily_any_val_t *any;
    struct lily_list_val_t *list;
    /* generic is a subset of any type that is refcounted. */
    struct lily_generic_val_t *generic;
    /* gc_generic is a subset of any type that is refcounted AND has a gc
       entry. */
    struct lily_generic_gc_val_t *gc_generic;
    struct lily_function_val_t *function;
    struct lily_hash_val_t *hash;
    struct lily_package_val_t *package;
    struct lily_instance_val_t *instance;
} lily_raw_value;

typedef struct lily_prop_entry_t {
    struct lily_sig_t *sig;
    int id;
    char *name;
    uint64_t name_shorthash;
    struct lily_prop_entry_t *next;
} lily_prop_entry;

/* lily_class represents a class in the language. Each class can have private
   members (call_start to call_top). */
typedef struct lily_class_t {
    char *name;
    /* This holds (up to) the first 8 bytes of the name. This is checked before
       doing a strcmp against the name. */
    uint64_t shorthash;
    int id;
    int flags;
    /* If true, then values of this class have a reference count. More on that
       in a bit. */
    int is_refcounted;
    /* During declaration, how many subclasses are allowed. List, for example,
       allows one class that will define what class the elements are. */
    int template_count;
    /* Signatures are used to represent the type of a var. Signatures are used
       to represent list[integer], hash[string, integer], and more. Classes are
       just classes.
       The sig of a class is the default signature. The integer class does not
       need a unique signature, as an example. Therefore, sig is set to a
       default that all can share.
       Sig is set only if the class is a simple one. */
    struct lily_sig_t *sig;
    struct lily_var_t *call_start;
    struct lily_var_t *call_top;

    struct lily_class_t *parent;
    lily_prop_entry *properties;
    int prop_start;

    /* Instead of loading all class members during init, Lily stores the needed
       information in the seed_table of a class. If the symtab can't find the
       name for a given class, then it's loaded into the vars of that class. */
    const struct lily_func_seed_t *seed_table;
    /* If not NULL, then setup_func will set seed_table since seed_table is
       typically a static const somewhere. */
    class_setup_func setup_func;
    gc_marker_func gc_marker;
    class_eq_func eq_func;
} lily_class;

/* Signatures are a bit more complicated. They're also very common. A signature
   stores a class and possibly other signatures.
   The symtab makes sure that when new signatures are created that they are all
   unique, allowing pointer compares of signatures. */
typedef struct lily_sig_t {
    lily_class *cls;
    /* If cls is the template class, this is the position within the parent.
       Hashes have two parts: a key and a value. 0 is the key, 1 is the value.
       Lists have one part, which is the element type. */
    int template_pos;

    /* Call arguments and call return type go here. Note that a call's return
       type is always at siglist[0]. */
    struct lily_sig_t **siglist;
    int siglist_size;
    int flags;

    /* The symtab stores all signatures in a linked list (symtab's root_sig)
       so they can be destroyed at exit. */
    struct lily_sig_t *next;
} lily_sig;



/* Next are the structs that emitter and symtab use to represent things. */



/* lily_sym is a subset of all symbol-related structs. Nothing should create
   values of this type. This is just for casting arguments. */
typedef struct lily_sym_t {
    int flags;
    lily_sig *sig;
    union lily_raw_value_t value;
    /* Every function has a set of registers that it puts the values it has into.
       Intermediate values (such as the result of addition or a function call),
       parameters, and variables.
       Note that functions do not go into registers, and are instead loaded
       like literals. */
    int reg_spot;
} lily_sym;

/* lily_literal holds string, number, and integer literals. */
typedef struct lily_literal_t {
    int flags;
    lily_sig *sig;
    lily_raw_value value;
    /* reg_spot is unused here, because literals are loaded by address. */
    int reg_spot;
    struct lily_literal_t *next;
} lily_literal;

/* lily_storage is a struct used by emitter to hold info for intermediate
   values (such as the result of an addition). The emitter will reuse these
   where possible. For example: If two different lines need to store an
   integer, then the same storage will be picked. However, reuse does not
   happen on the same line. */
typedef struct lily_storage_t {
    int flags;
    lily_sig *sig;
    /* This is provided to keep it a superset of lily_sym. */
    union lily_raw_value_t unused;
    int reg_spot;
    /* Each expression has a different expr_num. This prevents the same
       expression from using the same storage twice (which could lead to
       incorrect data). */
    int expr_num;
    struct lily_storage_t *next;
} lily_storage;

/* lily_var is used to represent a declared variable. */
typedef struct lily_var_t {
    int flags;
    lily_sig *sig;
    /* If this var is declared function, then the native function info is
       stored here. */
    union lily_raw_value_t value;
    int reg_spot;
    char *name;
    /* (Up to) the first 8 bytes of the name. This is compared before comparing
       the name. */
    uint64_t shorthash;
    /* The line on which this var was declared. If this is a builtin var, then
       line_num will be 0. */
    int line_num;
    /* How deep that functions were when this var was declared. If this is 1,
       then the var is in __main__ and a global. Otherwise, it is a local.
       This is an important difference, because the vm has to do different
       loads for globals versus locals. */
    int function_depth;
    struct lily_class_t *parent;
    struct lily_var_t *next;
} lily_var;



/* Now, values.  */



/* This is a string. It's pretty simple. These are refcounted. */
typedef struct lily_string_val_t {
    int refcount;
    char *string;
    int size;
} lily_string_val;

/* Next are anys. These are marked as refcounted, but that's just to keep
   them from being treated like simple values (such as integer or number).
   In reality, these copy their inner_value when assigning to other stuff.
   Because an any can hold any value, it has a gc entry to allow Lily's gc
   to check for circularity. */
typedef struct lily_any_val_t {
    int refcount;
    struct lily_gc_entry_t *gc_entry;
    struct lily_value_t *inner_value;
} lily_any_val;

/* This implements Lily's list, and tuple as well. The list class only allows
   the elements to have a single type. However, the tuple class allows for
   different types (but checking for the proper type).
   The gc_entry field is only set if the symtab determines that this particular
   list/tuple can become circular. */
typedef struct lily_list_val_t {
    int refcount;
    struct lily_gc_entry_t *gc_entry;
    struct lily_value_t **elems;
    int num_values;
    /* visited is used by lily_debug to make sure that it doesn't enter an
       infinite loop when trying to print info. */
    int visited;
} lily_list_val;

/* Lily's hashes are in two parts: The hash value, and the hash element. The
   hash element represents one key + value pair. */
typedef struct lily_hash_elem_t {
    /* Lily uses siphash2-4 to calculate the hash of given keys. This is the
       siphash for elem_key.*/
    uint64_t key_siphash;
    struct lily_value_t *elem_key;
    struct lily_value_t *elem_value;
    struct lily_hash_elem_t *next;
} lily_hash_elem;

/* Here's the hash value. Hashes are similar to lists in that there is only a
   gc entry if the associated signature is determined to be possibly circualar.
   Also, visited is there to protect lily_debug against a circular reference
   causing an infinite loop. */
typedef struct lily_hash_val_t {
    int refcount;
    struct lily_gc_entry_t *gc_entry;
    int visited;
    int num_elems;
    lily_hash_elem *elem_chain;
} lily_hash_val;

/* Packages hold vars of different types, and are created internally. */
typedef struct lily_package_val_t {
    int refcount;
    struct lily_gc_entry_t *gc_entry;
    lily_var **vars;
    int var_count;
    char *name;
} lily_package_val;

/* This represents an instance of a class. */
typedef struct lily_instance_val_t {
    int refcount;
    struct lily_gc_entry_t *gc_entry;
    struct lily_value_t **values;
    int num_values;
    int visited;
    /* This is used to determine what class this value really belongs to. For
       example, this value might be a SyntaxError instance set to a register of
       class Exception. */
    lily_class *true_class;
} lily_instance_val;

/* Finally, functions. Functions come in two flavors: Native and foreign.
   * Native:  This function is declared and defined by a user. It has a code
              section (which the vm will execute), and has to initialize
              registers that it will use.
   * Foreign: This function is automatically created by the interpreter. The
              implementation is defined outside of Lily code.

   These two are mutually exclusive (a function must never have a foreign_func
   set AND code). The interpreter makes no difference between either function
   (it's the vm's job to do the right call), so they're both passable as
   arguments to a function themselves. */
typedef struct lily_function_val_t {
    int refcount;

    /* The name of the class that this function belongs to OR "". */
    char *class_name;
    /* The name of this function, for use by debug and stack trace. */
    char *trace_name;

    /* Foreign functions only. To determine if a function is foreign, simply
       check 'foreign_func == NULL'. */
    lily_foreign_func foreign_func;

    /* Native functions only */

    /* Here's where the function's code is stored. */
    uintptr_t *code;
    /* This is where new instructions will get written to. It's for the
       emitter. */
    int pos;
    /* This is how much space the code has allocated (again for the emitter). */
    int len;
    /* This is used to initialize registers when entering this function.  */
    struct lily_register_info_t *reg_info;
    /* Finally, this is how many registers that this function uses. */
    int reg_count;
} lily_function_val;

/* Every value that is refcounted is a superset of this. */
typedef struct lily_generic_val_t {
    int refcount;
} lily_generic_val;

/* Every value that has a gc entry is a superset of this. */
typedef struct lily_generic_gc_val_t {
    int refcount;
    struct lily_gc_entry_t *gc_entry;
} lily_generic_gc_val;



/* Next, miscellanous structs. */



/* Here's a proper value in Lily. It has flags (for nil and other stuff), a
   sig, and the actual value. */
typedef struct lily_value_t {
    int flags;
    struct lily_sig_t *sig;
    lily_raw_value value;
} lily_value;

/* This is a gc entry. When these are created, the gc entry copies the value's
   raw value and the value's signature. It's important to NOT copy the value,
   because the value may be a register, and the sig will change. */
typedef struct lily_gc_entry_t {
    struct lily_sig_t *value_sig;
    /* If this is destroyed outside of the gc, then value.generic should be set
       to NULL to keep the gc from looking at an invalid data. */
    lily_raw_value value;
    /* Each gc pass has a different number that always goes up. If an entry's
       last_pass is not the current number, then the contained value is
       destroyed. */
    int last_pass;
    struct lily_gc_entry_t *next;
} lily_gc_entry;

/* This is used to initialize the registers that a function uses. It also holds
   names for doing trace. */
typedef struct lily_register_info_t {
    lily_sig *sig;
    char *class_name;
    char *name;
    int line_num;
} lily_register_info;

/* This holds all the information necessary to make a new Lily function. */
typedef struct lily_func_seed_t {
    char *name;
    lily_foreign_func func;
    const struct lily_func_seed_t *next;
    int arg_ids[];
} lily_func_seed;

/* This is used for seeding new properties. */
typedef struct lily_prop_seed_t {
    char *name;
    const struct lily_prop_seed_t *next;
    int prop_ids[];
} lily_prop_seed_t;

/* Finally, various definitions. */



/* CLS_* defines are for the flags of a lily_class. */
/* If this is set, the class can be used as a hash key. This should only be set
   on primitive and immutable classes. */
#define CLS_VALID_HASH_KEY 0x1


/* SIG_* defines are for the flags of a lily_sig. */
/* If set, the signature is either a vararg function. The last argument is the
   type for varargs. */
#define SIG_IS_VARARGS     0x1
/* If this is set, a gc entry is allocated for the type. This means that the
   value is a superset of lily_generic_gc_val_t. */
#define SIG_MAYBE_CIRCULAR 0x2


/* SYM_* defines are for identifying the type of symbol given. Emitter uses
   these sometimes. */
#define SYM_TYPE_LITERAL       0x01
#define SYM_TYPE_VAR           0x02
#define SYM_TYPE_STORAGE       0x04
/* This var is out of scope. This is set when a var in a non-function block
   goes out of scope. */
#define SYM_OUT_OF_SCOPE       0x10


/* VAR_* defines are meant mostly for the vm. However, emitter and symtab put
   VAR_IS_READONLY on vars that won't get a register. The vm will never see
   that flag. */

/* Don't put this in a register. This is used for functions, which are loaded
   as if they were literals. */
#define VAR_IS_READONLY        0x40
/* If this is set, the associated value should be treated as if it were unset,
   Don't ref/deref things which have this value associated with them.
   Anys: An any is nil if a value has not been allocated for it. If nil values
         are given to an any, then the any's inner_value should be set to
         nil. */
#define VAL_IS_NIL              0x100
/* If this is set, the associated value is valid, but should not get any refs
   or derefs. This is set on values that load literals to prevent literals from
   getting unnecessary refcount adjustments. */
#define VAL_IS_PROTECTED        0x200
/* For convenience, check for nil or protected set. */
#define VAL_IS_NIL_OR_PROTECTED 0x300


/* SYM_CLASS_* defines are for checking ids of a signature's class. These are
   used very frequently. These must be kept in sync with the class loading
   order given by lily_seed_symtab.h */
#define SYM_CLASS_INTEGER         0
#define SYM_CLASS_DOUBLE          1
#define SYM_CLASS_STRING          2
#define SYM_CLASS_FUNCTION        3
#define SYM_CLASS_ANY             4
#define SYM_CLASS_LIST            5
#define SYM_CLASS_HASH            6
#define SYM_CLASS_TUPLE           7
#define SYM_CLASS_TEMPLATE        8
#define SYM_CLASS_PACKAGE         9
#define SYM_CLASS_EXCEPTION      10
#define SYM_CLASS_NOMEMORYERROR  11
#define SYM_CLASS_DBZERROR       12 /* > 9000 */
#define SYM_CLASS_INDEXERROR     13
#define SYM_CLASS_BADTCERROR     14
#define SYM_CLASS_NORETURNERROR  15
#define SYM_CLASS_VALUEERROR     16
#define SYM_CLASS_RECURSIONERROR 17
#define SYM_CLASS_KEYERROR       18
#define SYM_CLASS_FORMATERROR    19

/* TODO: Make classes into a linked list so this can go away. */
#define SYM_LAST_CLASS      19
#define INITIAL_CLASS_SIZE  20

#endif
