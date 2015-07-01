#ifndef LILY_CORE_TYPES_H
# define LILY_CORE_TYPES_H

# include <stdlib.h>
# include <stdint.h>
# include <stdio.h>

/* This file defines all core types used by the language. Many of these types
   reference each other. Some are left incomplete, because not every part of
   the interpreter uses all of them. */

struct lily_class_;
struct lily_vm_state_;
struct lily_value_;
struct lily_var_;
struct lily_type_;
struct lily_register_info_;
struct lily_function_val_;
struct lily_symtab_;
struct lily_parse_state_;

/* gc_marker_func is a function called to mark all values within a given value.
   The is used by the gc to mark values as being visited. Values not visited
   will be collected. */
typedef void (*gc_marker_func)(int, struct lily_value_ *);
/* Lily's foreign functions look like this. */
typedef void (*lily_foreign_func)(struct lily_vm_state_ *, uint16_t,
        uint16_t *);
/* This is called to do == and != when the vm has complex values, and also for
   comparing values held in an any. The vm is passed as a guard against
   an infinite loop. */
typedef int (*class_eq_func)(struct lily_vm_state_ *, int *,
        struct lily_value_ *, struct lily_value_ *);
/* This function is called when a value tagged as refcounted drops to 0 refs.
   This handles a value of a given class (regardless of type) and frees what is
   inside. */
typedef void (*class_destroy_func)(struct lily_value_ *);
/* This function is called to initialize seeds of type dyna_var. */
typedef void (*var_loader)(struct lily_parse_state_ *, struct lily_var_ *);

/* lily_raw_value is a union of all possible values, plus a bit more. This is
   not common, because lily_value (which has flags and a type) is typically
   used for parameters and such instead. However, this does have some uses. */
typedef union lily_raw_value_ {
    int64_t integer;
    double doubleval;
    struct lily_string_val_ *string;
    struct lily_any_val_ *any;
    struct lily_list_val_ *list;
    /* generic is a subset of any type that is refcounted. */
    struct lily_generic_val_ *generic;
    /* gc_generic is a subset of any type that is refcounted AND has a gc
       entry. */
    struct lily_generic_gc_val_ *gc_generic;
    struct lily_function_val_ *function;
    struct lily_hash_val_ *hash;
    struct lily_file_val_ *file;
    struct lily_instance_val_ *instance;
} lily_raw_value;

/* lily_class represents a class in the language. */
typedef struct lily_class_ {
    uint64_t flags;

    char *name;
    /* This holds (up to) the first 8 bytes of the name. This is checked before
       doing a strcmp against the name. */
    uint64_t shorthash;

    /* The type of the var stores the complete type knowledge of the var. */
    struct lily_type_ *type;

    /* This function is used to destroy values of this class, regardless of
       their type. Since the vm calls this semi-often, put this somewhere higher
       up. */
    class_destroy_func destroy_func;

    /* This is a linked list of all methods that are within this function. This
       is NULL if there are no methods. */
    struct lily_var_ *call_chain;

    struct lily_class_ *parent;
    struct lily_class_ *next;

    struct lily_prop_entry_ *properties;

    /* If it's an enum class, then the variants are here. NULL otherwise. */
    struct lily_class_ **variant_members;

    uint16_t id;
    uint16_t pad;
    uint16_t is_refcounted;
    /* If positive, how many subtypes are allowed in this type. This can also
       be -1 if an infinite number of types are allowed (ex: functions). */
    int16_t generic_count;
    uint32_t prop_count;
    uint32_t variant_size;
    /* If the variant class takes arguments, then this is the type of a
       function that maps from input to the result.
       If the variant doesn't take arguments, then this is a simple type
       that just defines the class (like a default type). */
    struct lily_type_ *variant_type;

    /* This is the package that this class was defined within. This is used to
       print a proper package name for classes.  */
    struct lily_import_entry_ *import;

    /* This holds class methods that may or may not have been loaded (non-native
       classes only). */
    const void *dynaload_table;
    gc_marker_func gc_marker;
    class_eq_func eq_func;
} lily_class;

typedef struct lily_type_ {
    lily_class *cls;

    /* If this type has subtypes (ex: A list has a subtype that explains what
       type is allowed inside), then this is where those subtypes are.
       Functions are a special case, where subtypes[0] is either their return
       type, or NULL. */
    struct lily_type_ **subtypes;

    uint32_t subtype_count;
    /* If this type is for a generic, then this is the id of that generic.
       A = 0, B = 1, C = 2, etc. */
    uint16_t generic_pos;
    uint16_t flags;

    /* All types are stored in a linked list in the symtab so they can be
       easily destroyed. */
    struct lily_type_ *next;
} lily_type;



/* Next are the structs that emitter and symtab use to represent things. */



/* This is a superset of lily_class, as well as everything that lily_sym is a
   superset of. */
typedef struct {
    uint64_t flags;
} lily_item;

/* lily_sym is a subset of all symbol-related structs. Nothing should create
   values of this type. This is just for casting arguments. */
typedef struct lily_sym_ {
    uint64_t flags;
    lily_type *type;
    /* Every function has a set of registers that it puts the values it has into.
       Intermediate values (such as the result of addition or a function call),
       parameters, and variables.
       Note that functions do not go into registers, and are instead loaded
       like literals. */
    uint32_t reg_spot;
    uint32_t unused_pad;
} lily_sym;

/* This represents a property within a class that isn't "primitive" to the
   interpreter (lists, tuples, integer, string, etc.).
   User-defined classes and Exception both support these. */
typedef struct lily_prop_entry_ {
    uint64_t flags;
    struct lily_type_ *type;
    uint32_t id;
    uint32_t pad;
    char *name;
    uint64_t name_shorthash;
    lily_class *cls;
    struct lily_prop_entry_ *next;
} lily_prop_entry;

/* A tie represents an association between some particular spot, and a value
   given. This struct represents literals, readonly vars, and foreign values. */
typedef struct lily_tie_ {
    uint64_t flags;
    lily_type *type;
    uint32_t reg_spot;
    uint32_t pad;
    lily_raw_value value;
    struct lily_tie_ *next;
} lily_tie;

/* lily_storage is a struct used by emitter to hold info for intermediate
   values (such as the result of an addition). The emitter will reuse these
   where possible. For example: If two different lines need to store an
   integer, then the same storage will be picked. However, reuse does not
   happen on the same line. */
typedef struct lily_storage_ {
    uint64_t flags;
    lily_type *type;
    uint32_t reg_spot;
    /* Each expression has a different expr_num. This prevents the same
       expression from using the same storage twice (which could lead to
       incorrect data). */
    uint32_t expr_num;
    struct lily_storage_ *next;
} lily_storage;

/* lily_var is used to represent a declared variable. */
typedef struct lily_var_ {
    uint64_t flags;
    lily_type *type;
    uint32_t reg_spot;
    /* The line on which this var was declared. If this is a builtin var, then
       line_num will be 0. */
    uint32_t line_num;
    char *name;
    /* (Up to) the first 8 bytes of the name. This is compared before comparing
       the name. */
    uint64_t shorthash;
    /* How deep that functions were when this var was declared. If this is 1,
       then the var is in __main__ and a global. Otherwise, it is a local.
       This is an important difference, because the vm has to do different
       loads for globals versus locals. */
    uint32_t function_depth;
    uint32_t pad;
    struct lily_class_ *parent;
    struct lily_var_ *next;
} lily_var;


/* Now, values.  */



/* This is a string. It's pretty simple. These are refcounted. */
typedef struct lily_string_val_ {
    uint32_t refcount;
    uint32_t size;
    char *string;
} lily_string_val;

/* Next are anys. These are marked as refcounted, but that's just to keep
   them from being treated like simple values (such as integer or number).
   In reality, these copy their inner_value when assigning to other stuff.
   Because an any can hold any value, it has a gc entry to allow Lily's gc
   to check for circularity. */
typedef struct lily_any_val_ {
    uint32_t refcount;
    uint32_t pad;
    struct lily_gc_entry_ *gc_entry;
    struct lily_value_ *inner_value;
} lily_any_val;

/* This implements Lily's list, and tuple as well. The list class only allows
   the elements to have a single type. However, the tuple class allows for
   different types (but checking for the proper type).
   The gc_entry field is only set if the symtab determines that this particular
   list/tuple can become circular. */
typedef struct lily_list_val_ {
    uint32_t refcount;
    uint32_t pad;
    struct lily_gc_entry_ *gc_entry;
    struct lily_value_ **elems;
    uint32_t num_values;
    /* visited is used by lily_debug to make sure that it doesn't enter an
       infinite loop when trying to print info. */
    uint32_t visited;
} lily_list_val;

/* Lily's hashes are in two parts: The hash value, and the hash element. The
   hash element represents one key + value pair. */
typedef struct lily_hash_elem_ {
    /* Lily uses siphash2-4 to calculate the hash of given keys. This is the
       siphash for elem_key.*/
    uint64_t key_siphash;
    struct lily_value_ *elem_key;
    struct lily_value_ *elem_value;
    struct lily_hash_elem_ *next;
} lily_hash_elem;

/* Here's the hash value. Hashes are similar to lists in that there is only a
   gc entry if the associated typenature is determined to be possibly circualar.
   Also, visited is there to protect lily_debug against a circular reference
   causing an infinite loop. */
typedef struct lily_hash_val_ {
    uint32_t refcount;
    uint32_t pad;
    struct lily_gc_entry_ *gc_entry;
    uint32_t visited;
    uint32_t num_elems;
    lily_hash_elem *elem_chain;
} lily_hash_val;

/* This represents an instance of a class. */
typedef struct lily_instance_val_ {
    uint32_t refcount;
    uint32_t pad;
    struct lily_gc_entry_ *gc_entry;
    struct lily_value_ **values;
    int num_values;
    int visited;
    /* This is used to determine what class this value really belongs to. For
       example, this value might be a SyntaxError instance set to a register of
       class Exception. */
    lily_class *true_class;
} lily_instance_val;

typedef struct lily_file_val_ {
    uint32_t refcount;
    uint32_t is_open;
    uint32_t read_ok;
    uint32_t write_ok;
    FILE *inner_file;
} lily_file_val;

typedef struct {
    uint32_t refcount;
    uint32_t num_upvalues;
    struct lily_value_ **upvalues;
} lily_closure_data;

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
typedef struct lily_function_val_ {
    uint32_t refcount;
    uint32_t line_num;

    struct lily_gc_entry_ *gc_entry;

    /* The name of the class that this function belongs to OR "". */
    char *class_name;
    /* The name of this function, for use by debug and stack trace. */
    char *trace_name;
    /* The import that this function was created within. */
    struct lily_import_entry_ *import;

    /* Foreign functions only. To determine if a function is foreign, simply
       check 'foreign_func == NULL'. */
    lily_foreign_func foreign_func;

    /* Native functions only */

    /* Here's where the function's code is stored. */
    uint16_t *code;

    /* This is how much code is in this particular function. */
    uint32_t len;

    uint32_t pad;

    lily_closure_data *closure_data;
    /* Does this function contain generics? If so, they may need to be solved
       at vm-time when it's called. */
    uint32_t has_generics;
    /* This is how many registers that this function uses. */
    uint32_t reg_count;
    /* This is used to initialize registers when entering this function.  */
    struct lily_register_info_ *reg_info;
} lily_function_val;

/* Every value that is refcounted is a superset of this. */
typedef struct lily_generic_val_ {
    uint32_t refcount;
} lily_generic_val;

/* Every value that has a gc entry is a superset of this. */
typedef struct lily_generic_gc_val_ {
    uint32_t refcount;
    uint32_t pad;
    struct lily_gc_entry_ *gc_entry;
} lily_generic_gc_val;



/* Next, miscellanous structs. */



/* Here's a proper value in Lily. It has flags (for nil and other stuff), a
   type, and the actual value. */
typedef struct lily_value_ {
    uint64_t flags;
    struct lily_type_ *type;
    lily_raw_value value;
} lily_value;

/* This is a gc entry. When these are created, the gc entry copies the value's
   raw value and the value's type. It's important to NOT copy the value,
   because the value may be a register, and the type will change. */
typedef struct lily_gc_entry_ {
    struct lily_type_ *value_type;
    /* If this is destroyed outside of the gc, then value.generic should be set
       to NULL to keep the gc from looking at an invalid data. */
    lily_raw_value value;
    /* Each gc pass has a different number that always goes up. If an entry's
       last_pass is not the current number, then the contained value is
       destroyed. */
    uint32_t last_pass;
    uint32_t pad;
    struct lily_gc_entry_ *next;
} lily_gc_entry;

/* This is used to initialize the registers that a function uses. It also holds
   names for doing trace. */
typedef struct lily_register_info_ {
    lily_type *type;
    char *name;
    uint32_t line_num;
    uint32_t pad;
} lily_register_info;

typedef struct lily_import_link_ {
    struct lily_import_entry_ *entry;
    char *as_name;
    struct lily_import_link_ *next_import;
} lily_import_link;

/* This struct holds information for when an import references a
   library. */
typedef struct {
    /* This is the handle to the library. */
    void *source;
    /* This is the first link in the module's dynaloads. */
    const void *dynaload_table;
} lily_library;

/* This is used to manage information about imports. */
typedef struct lily_import_entry_ {
    /* The name given to import this thing. */
    char *loadname;

    /* The path used to load this file. */
    char *path;

    lily_import_link *import_chain;

    /* The classes that were declared within the imported file. */
    lily_class *class_chain;

    /* The vars within the imported file. */
    lily_var *var_chain;

    /* This is non-NULL if 'import' found a dynamic library to open in place of
       a normal module. */
    lily_library *library;

    /* For builtin imports, this can contain classes, vars, or functions to
       dynaload. */
    const void *dynaload_table;

    /* If the import provides seeds of type dyna_var, this is called with the
       name when the given name is referenced. */
    var_loader var_load_fn;

    /* Every import entry that is created is linked to each other starting from
       this one. */
    struct lily_import_entry_ *root_next;
} lily_import_entry;

/* This structure defines a series of options */
typedef struct lily_options_ {
    /* For now, this should be '1'. */
    uint32_t version;
    /* The maximum number of values the gc should have marked before
       attempting a sweep. This should be at least 2. */
    uint32_t gc_threshold;
    /* # of entries in the argv. */
    uint64_t argc;
    /* This is made available as sys::argv when sys is imported. By default,
       this is NULL and sys::argv is empty. */
    char **argv;
    /* Lily will call lily_impl_puts with this as the data part. This
       can be NULL if it's not needed.
       This is used by mod_lily to hold Apache's request_rec. */
    void *data;
} lily_options;

/* Finally, various definitions. */


/* ITEM_* defines are used to determine what lily_sym and lily_item can
   be cast to.
   To prevent potential clashes, the definitions afterward (except for
   type) start off where these end. */
#define ITEM_TYPE_TIE           0x01
#define ITEM_TYPE_VAR           0x02
#define ITEM_TYPE_STORAGE       0x04
#define ITEM_TYPE_VARIANT_CLASS 0x10
#define ITEM_TYPE_PROPERTY      0x20


/* CLS_* defines are for lily_class. */


#define CLS_VALID_HASH_KEY 0x0100
#define CLS_ENUM_CLASS     0x0200
#define CLS_VARIANT_CLASS  0x0400
/* This class is an enum class AND the variants within are scoped. The
   difference is that scoped variants are accessed using 'enum::variant',
   while normal variants can use just 'variant'. */
#define CLS_ENUM_IS_SCOPED 0x1000


/* TYPE_* defines are for lily_type.
   Since types are not usable as values, they do not need to start where
   the ITEM_* defines leave off. */


/* If set, the type is a function that takes a variable number of values. */
#define TYPE_IS_VARARGS        0x01
/* If this is set, a gc entry is allocated for the type. This means that the
   value is a superset of lily_generic_gc_val_t. */
#define TYPE_MAYBE_CIRCULAR    0x02
/* The symtab puts this flag onto generic types which aren't currently
   available. So if there are 4 generic types available but only 2 used, it
   simply hides the second two from being returned. */
#define TYPE_HIDDEN_GENERIC    0x04
/* This is set on a type when it is a generic (ex: A, B, ...), or when it
   contains generics at some point. Emitter and vm use this as a fast way of
   checking if a type needs to be resolved or not. */
#define TYPE_IS_UNRESOLVED     0x10
/* This is set on function types that have at least one optional argument. This
   is set so that emitter and ts can easily figure out if the function doesn't
   have to take some arguments. */
#define TYPE_HAS_OPTARGS       0x20


/* SYM_* flags are for things based off of lily_sym. */


/* properties, vars: This is used to prevent a value from being used to
   initialize itself. */
#define SYM_NOT_INITIALIZED     0x100
/* storages: This is set when the result of some expression cannot be assigned
   to. This is to prevent things like '[1,2,3][0] = 4'. */
#define SYM_NOT_ASSIGNABLE      0x200

#define SYM_CLOSED_OVER         0x400

/* VAR_* flags are for vars. Since these have lily_sym as a superset, they begin
   where lily_sym's flags leave off. */


/* This is a var that is no longer in scope. It is kept around until the
   function it is within is done so type information can be loaded up into the
   registers later. */
#define VAR_OUT_OF_SCOPE        0x1000

/* This is set on vars which will be used to hold the value of a defined
   function, a lambda, or a class constructor. Vars with this flag cannot be
   assigned to. Additionally, the reg_spot they contain is actually a spot in
   the vm's 'readonly_table'. */
#define VAR_IS_READONLY         0x2000


/* VAL_* flags are for lily_value. */


/* This is set on when there is no -appropriate- data in the inner part of the
   value. This flag exists to prevent unnecessary or invalid attempts to deref
   the contents of a value. The vm sets values to (integer) 0 beforehand to
   prevent an accidental invalid read, however.
   If this flag is set, do not ref or deref the contents. */
#define VAL_IS_NIL              0x10000
/* This particular value has been assigned a value that is either a literal or
   a defined function. Do not ref or deref this value. */
#define VAL_IS_PROTECTED        0x20000
/* Values of this type are not refcounted. */
#define VAL_IS_PRIMITIVE        0x40000
/* Check if a value is nil, protected, or not refcounted. If any of those is
   true, then the given value should not get a deref.
   There are some cases in the vm where only one of the above flags is set. This
   is intentional, as it only takes one flag to be unable to deref. */
#define VAL_IS_NOT_DEREFABLE    0x70000


/* SYM_CLASS_* defines are for checking ids of a type's class. These are
   used very frequently. These must be kept in sync with the class loading
   order given by lily_pkg_builtin.c */
#define SYM_CLASS_INTEGER         0
#define SYM_CLASS_DOUBLE          1
#define SYM_CLASS_STRING          2
#define SYM_CLASS_BYTESTRING      3
#define SYM_CLASS_FUNCTION        4
#define SYM_CLASS_ANY             5
#define SYM_CLASS_LIST            6
#define SYM_CLASS_HASH            7
#define SYM_CLASS_TUPLE           8
#define SYM_CLASS_OPTARG          9
#define SYM_CLASS_FILE           10
#define SYM_CLASS_GENERIC        11
#define SYM_CLASS_EXCEPTION      12

#endif
