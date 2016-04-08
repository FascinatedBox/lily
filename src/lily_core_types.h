#ifndef LILY_CORE_TYPES_H
# define LILY_CORE_TYPES_H

# include <stdlib.h>
# include <stdint.h>
# include <stdio.h>

/* This file defines all core types used by the language. Many of these types
   reference each other. Some are left incomplete, because not every part of
   the interpreter uses all of them. */

struct lily_vm_state_;
struct lily_value_;
struct lily_var_;
struct lily_type_;
struct lily_function_val_;
struct lily_parse_state_;

/* Lily's foreign functions look like this. */
typedef void (*lily_foreign_func)(struct lily_vm_state_ *, uint16_t,
        uint16_t *);
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
    struct lily_dynamic_val_ *dynamic;
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
    struct lily_foreign_val_ *foreign;
} lily_raw_value;

typedef struct {
    struct lily_class_ *next;

    uint16_t item_kind;
    uint16_t flags;
    uint16_t variant_id;
    uint16_t pad;

    char *name;
    uint64_t shorthash;

    union {
        struct lily_type_ *build_type;
        struct lily_tie_ *default_value;
    };

    struct lily_class_ *parent;
} lily_variant_class;

/* lily_class represents a class in the language. */
typedef struct lily_class_ {
    struct lily_class_ *next;

    uint16_t item_kind;
    uint16_t flags;
    uint32_t move_flags;

    char *name;
    /* This holds (up to) the first 8 bytes of the name. This is checked before
       doing a strcmp against the name. */
    uint64_t shorthash;

    /* The type of the var stores the complete type knowledge of the var. */
    struct lily_type_ *type;

    /* This is a linked list of all methods that are within this function. This
       is NULL if there are no methods. */
    struct lily_var_ *call_chain;

    struct lily_class_ *parent;

    struct lily_prop_entry_ *properties;

    /* If it's an enum, then the variants are here. NULL otherwise. */
    lily_variant_class **variant_members;

    uint16_t id;
    uint16_t is_builtin;
    uint16_t is_refcounted;
    /* If positive, how many subtypes are allowed in this type. This can also
       be -1 if an infinite number of types are allowed (ex: functions). */
    int16_t generic_count;
    uint16_t prop_count;
    uint16_t variant_size;

    /* Enums: This is how many subvalues (slots) that the vm must allocate for
       this if it's an enum. */
    uint16_t enum_slot_count;

    /* Enums and classes: This is the type that 'self' will have. For those
       without generics, this is the default type. */
    struct lily_type_ *self_type;

    /* This is the package that this class was defined within. This is used to
       print a proper package name for classes.  */
    struct lily_import_entry_ *import;

    /* This contains all types which have this class as their class. */
    struct lily_type_ *all_subtypes;

    const void *dynaload_table;
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
    void *pad;
    uint16_t item_kind;
    uint16_t flags;
    uint32_t pad2;
} lily_item;

/* lily_sym is a subset of all symbol-related structs. Nothing should create
   values of this type. This is just for casting arguments. */
typedef struct lily_sym_ {
    void *pad;
    uint16_t item_kind;
    uint16_t flags;
    /* Every function has a set of registers that it puts the values it has into.
       Intermediate values (such as the result of addition or a function call),
       parameters, and variables.
       Note that functions do not go into registers, and are instead loaded
       like literals. */
    uint32_t reg_spot;
    lily_type *type;
} lily_sym;

/* This represents a property within a class that isn't "primitive" to the
   interpreter (lists, tuples, integer, string, etc.).
   User-defined classes and Exception both support these. */
typedef struct lily_prop_entry_ {
    struct lily_prop_entry_ *next;
    uint16_t item_kind;
    uint16_t flags;
    uint32_t id;
    struct lily_type_ *type;
    char *name;
    uint64_t name_shorthash;
    lily_class *cls;
} lily_prop_entry;

/* A tie represents an association between some particular spot, and a value
   given. This struct represents literals, readonly vars, and foreign values. */
typedef struct lily_tie_ {
    struct lily_tie_ *next;
    uint16_t item_kind;
    uint16_t flags;
    uint32_t reg_spot;
    lily_type *type;
    uint32_t pad;
    uint32_t move_flags;
    lily_raw_value value;
} lily_tie;

/* lily_storage is a struct used by emitter to hold info for intermediate
   values (such as the result of an addition). The emitter will reuse these
   where possible. For example: If two different lines need to store an
   integer, then the same storage will be picked. However, reuse does not
   happen on the same line. */
typedef struct lily_storage_ {
    struct lily_storage_ *next;
    uint16_t item_kind;
    uint16_t flags;
    uint32_t reg_spot;
    /* Each expression has a different expr_num. This prevents the same
       expression from using the same storage twice (which could lead to
       incorrect data). */
    lily_type *type;
    uint32_t expr_num;
} lily_storage;

/* lily_var is used to represent a declared variable. */
typedef struct lily_var_ {
    struct lily_var_ *next;
    uint16_t item_kind;
    uint16_t flags;
    uint32_t reg_spot;
    lily_type *type;
    /* The line on which this var was declared. If this is a builtin var, then
       line_num will be 0. */
    uint32_t line_num;
    /* How deep that functions were when this var was declared. If this is 1,
       then the var is in __main__ and a global. Otherwise, it is a local.
       This is an important difference, because the vm has to do different
       loads for globals versus locals. */
    uint32_t function_depth;
    char *name;
    /* (Up to) the first 8 bytes of the name. This is compared before comparing
       the name. */
    uint64_t shorthash;
    struct lily_class_ *parent;
} lily_var;


/* Now, values.  */



/* This is a string. It's pretty simple. These are refcounted. */
typedef struct lily_string_val_ {
    uint32_t refcount;
    uint32_t size;
    char *string;
} lily_string_val;

/* These are values for the Dynamic class. They're always refcounted. Since the
   contents are unknown, Dynamic values always have a gc tag set on them. */
typedef struct lily_dynamic_val_ {
    uint32_t refcount;
    uint32_t pad;
    struct lily_gc_entry_ *gc_entry;
    struct lily_value_ *inner_value;
} lily_dynamic_val;

/* This implements Lily's list, and tuple as well. The list class only allows
   the elements to have a single type. However, the tuple class allows for
   different types (but checking for the proper type).
   The gc_entry field is only set if the symtab determines that this particular
   list/tuple can become circular. */
typedef struct lily_list_val_ {
    uint32_t refcount;
    uint32_t extra_space;
    struct lily_gc_entry_ *gc_entry;
    struct lily_value_ **elems;
    uint32_t num_values;
    uint32_t pad;
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
    struct lily_hash_elem_ *prev;
} lily_hash_elem;

typedef struct lily_hash_val_ {
    uint32_t refcount;
    uint32_t iter_count;
    struct lily_gc_entry_ *gc_entry;
    uint32_t pad;
    uint32_t num_elems;
    lily_hash_elem *elem_chain;
} lily_hash_val;

/* This represents either a class instance or an enum. */
typedef struct lily_instance_val_ {
    uint32_t refcount;
    uint16_t instance_id;
    uint16_t variant_id;
    struct lily_gc_entry_ *gc_entry;
    struct lily_value_ **values;
    uint32_t num_values;
    uint32_t pad;
} lily_instance_val;

typedef struct lily_file_val_ {
    uint32_t refcount;
    uint8_t read_ok;
    uint8_t write_ok;
    uint8_t is_builtin;
    uint8_t pad1;
    uint32_t pad2;
    FILE *inner_file;
} lily_file_val;

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

    uint32_t pad;

    uint16_t num_upvalues;

    /* This is how many registers that this function uses. */
    uint16_t reg_count;

    struct lily_value_ **upvalues;
} lily_function_val;

/* Every value that is refcounted is a superset of this. */
typedef struct lily_generic_val_ {
    uint32_t refcount;
} lily_generic_val;

/* Foreign values must start with at least this layout. Note that this carries
   the implicit assumption that no foreign value will carry Lily values inside.
   Should that change, this too many need to be changed. */
typedef struct lily_foreign_val_ {
    uint32_t refcount;
    uint32_t pad;
    class_destroy_func destroy_func;
} lily_foreign_val;

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
    uint32_t flags;
    /* This field is ignored unless this value is an upvalue within a function
       value. If it is, then each closure that uses this cell bumps the refcount
       here. When cell_refcount is zero, the raw value is deref'd and the value
       itself is destroyed. */
    uint32_t cell_refcount;
    lily_raw_value value;
} lily_value;

/* This holds a value that has been deemed interesting to the gc. This has the
   same layout as a lily_value_ on purpose. */
typedef struct lily_gc_entry_ {
    /* The flags from the value. Used to determine what's inside the value. */
    uint32_t flags;
    /* Each gc pass has a different number that always goes up. If an entry's
       last_pass is not the current number, then the contained value is
       destroyed. */
    int32_t last_pass;
    /* If this is destroyed outside of the gc, then value.generic should be set
       to NULL to keep the gc from looking at an invalid data. */
    lily_raw_value value;
    struct lily_gc_entry_ *next;
} lily_gc_entry;

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
    /* Every import entry that is created is linked to each other starting from
       this one. */
    struct lily_import_entry_ *root_next;

    /* This allows imports to be cast as lily_item, which is used during parser
       dynaloading. */
    uint32_t item_kind;
    uint16_t cid_start;
    uint16_t pad;

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
} lily_import_entry;

/* This structure defines a series of options for initializing the interpeter.
   Defaults can be found by searching for lily_new_default_options within
   lily_parser.c. */
typedef struct lily_options_ {
    /* For now, this should be '1'. */
    uint8_t version;
    /* How much should the current number of allowed gc entries be multiplied by
       if unable to free anything. */
    uint8_t gc_multiplier;
    uint16_t argc;
    /* The initial maximum amount of entries allowed to have a gc tag before
       asking for another causes a sweep. */
    uint32_t gc_start;
    /* This is made available as sys.argv when sys is imported. By default,
       this is NULL and sys.argv is empty. */
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
#define ITEM_TYPE_TIE      1
#define ITEM_TYPE_VAR      2
#define ITEM_TYPE_STORAGE  3
#define ITEM_TYPE_VARIANT  4
#define ITEM_TYPE_PROPERTY 5
#define ITEM_TYPE_IMPORT   6


/* CLS_* defines are for lily_class. */


#define CLS_VALID_HASH_KEY 0x01
#define CLS_VALID_OPTARG   0x02
#define CLS_IS_ENUM        0x04
#define CLS_IS_VARIANT     0x10
/* This class is an enum AND the variants within are scoped. The difference is
   that scoped variants are accessed using 'enum.variant', while normal
   variants can use just 'variant'. */
#define CLS_ENUM_IS_SCOPED 0x20
#define CLS_EMPTY_VARIANT  0x40

/* TYPE_* defines are for lily_type.
   Since types are not usable as values, they do not need to start where
   the ITEM_* defines leave off. */


/* If set, the type is a function that takes a variable number of values. */
#define TYPE_IS_VARARGS        0x01
/* The symtab puts this flag onto generic types which aren't currently
   available. So if there are 4 generic types available but only 2 used, it
   simply hides the second two from being returned. */
#define TYPE_HIDDEN_GENERIC    0x02
/* This is set on a type when it is a generic (ex: A, B, ...), or when it
   contains generics at some point. Emitter and vm use this as a fast way of
   checking if a type needs to be resolved or not. */
#define TYPE_IS_UNRESOLVED     0x04
/* This is set on function types that have at least one optional argument. This
   is set so that emitter and ts can easily figure out if the function doesn't
   have to take some arguments. */
#define TYPE_HAS_OPTARGS       0x10
/* This is set on a type that either is the ? type, or has a type that contains
   the ? type within it. */
#define TYPE_IS_INCOMPLETE     0x20

/* SYM_* flags are for things based off of lily_sym. */


/* properties, vars: This is used to prevent a value from being used to
   initialize itself. */
#define SYM_NOT_INITIALIZED     0x01
/* storages: This is set when the result of some expression cannot be assigned
   to. This is to prevent things like '[1,2,3][0] = 4'. */
#define SYM_NOT_ASSIGNABLE      0x02

#define SYM_CLOSED_OVER         0x04

/* properties, vars: This is 'private' to the class it was declared within. */
#define SYM_SCOPE_PRIVATE       0x10

/* properties, vars: This is 'protected' to the class it was declared within. */
#define SYM_SCOPE_PROTECTED     0x20

/* There is no 'SYM_SCOPE_PUBLIC', because public is the default. */

/* VAR_* flags are for vars. Since these have lily_sym as a superset, they begin
   where lily_sym's flags leave off. */


/* This is a var that is no longer in scope. It is kept around until the
   function it is within is done so type information can be loaded up into the
   registers later. */
#define VAR_OUT_OF_SCOPE        0x040

/* This is set on vars which will be used to hold the value of a defined
   function, a lambda, or a class constructor. Vars with this flag cannot be
   assigned to. Additionally, the reg_spot they contain is actually a spot in
   the vm's 'readonly_table'. */
#define VAR_IS_READONLY         0x100

/* This flag is set on defined functions that are found inside of other defined
   functions. Calling a function with this tag may involve the use of closures,
   so the emitter needs to wrap the given function so that it will have closure
   information. */
#define VAR_NEEDS_CLOSURE       0x200


/* VAL_* flags are for lily_value. */


#define VAL_IS_BOOLEAN          0x000001
#define VAL_IS_INTEGER          0x000002
#define VAL_IS_DOUBLE           0x000004
#define VAL_IS_STRING           0x000010
#define VAL_IS_BYTESTRING       0x000020
#define VAL_IS_FUNCTION         0x000040
#define VAL_IS_DYNAMIC          0x000100
#define VAL_IS_LIST             0x000200
#define VAL_IS_HASH             0x000400
#define VAL_IS_TUPLE            0x001000
#define VAL_IS_INSTANCE         0x002000
#define VAL_IS_ENUM             0x004000
#define VAL_IS_FILE             0x010000
#define VAL_IS_DEREFABLE        0x020000
#define VAL_IS_GC_TAGGED        0x040000
#define VAL_IS_FOREIGN          0x100000
/* This is a raw string that shouldn't be quoted during interpolation. */
#define VAL_IS_FOR_INTERP       0x200000

/* SYM_CLASS_* defines are for checking ids of a type's class. These are
   used very frequently. These must be kept in sync with the class loading
   order given by lily_pkg_builtin.c */
#define SYM_CLASS_INTEGER         0
#define SYM_CLASS_DOUBLE          1
#define SYM_CLASS_STRING          2
#define SYM_CLASS_BYTESTRING      3
#define SYM_CLASS_BOOLEAN         4
#define SYM_CLASS_FUNCTION        5
#define SYM_CLASS_DYNAMIC         6
#define SYM_CLASS_LIST            7
#define SYM_CLASS_HASH            8
#define SYM_CLASS_TUPLE           9
#define SYM_CLASS_OPTARG         10
#define SYM_CLASS_FILE           11
#define SYM_CLASS_GENERIC        12
#define SYM_CLASS_QUESTION       13
#define SYM_CLASS_OPTION         14
#define SYM_CLASS_EITHER         15
#define START_CLASS_ID           16
/* Exception+Tainted boot are bootstrapped in as a class and enum respectively.
   Their ids have to start after the internal start. */
#define SYM_CLASS_EXCEPTION      16
#define SYM_CLASS_TAINTED        17

#endif
