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
struct lily_foreign_tie_;
struct lily_options_;

/* Lily's foreign functions look like this. */
typedef void (*lily_foreign_func)(struct lily_vm_state_ *, uint16_t,
        uint16_t *);
/* This function is called when a value tagged as refcounted drops to 0 refs.
   This handles a value of a given class (regardless of type) and frees what is
   inside. */
typedef void (*class_destroy_func)(struct lily_value_ *);
/* This function is called to initialize seeds of type dyna_var. */
typedef void (*var_loader)(struct lily_parse_state_ *, const char *,
        struct lily_foreign_tie_ *);

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

/* A proper Lily value. The 'flags' field holds gc/deref info, as well as a
   VAL_IS_* flag to indicate the -kind- of value. */
typedef struct lily_value_ {
    uint32_t flags;
    /* This is only used by closure cells. When a closure cell has a
       cell_refcount of zero, it's deref'd and free'd. */
    uint32_t cell_refcount;
    lily_raw_value value;
} lily_value;

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

    /* This is the module that this class was defined within. This is sometimes
       used for establishing a scope when doing dynaloading. */
    struct lily_module_entry_ *module;

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
   given. This struct represents literals, and defined functions. */
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

/* A foreign tie associates a dynaloaded var with a particular register spot.
   Foreign ties are always loaded as globals so that they are available in any
   scope. The difference between this and lily_tie is that foreign ties have a
   non-pointer 'data' field for the value instead of having the value but as
   different fields.
   This split was not done without reason. Emitter often needs to reach into the
   raw value of a tie (ex: Tuple subscripts). So having a '.data' in the way is
   annoying at best.
   Modules, on the other hand, can't use the move api with inlined fields. */
typedef struct lily_foreign_tie_ {
    struct lily_foreign_tie_ *next;
    uint16_t item_kind;
    uint16_t flags;
    uint32_t reg_spot;
    lily_type *type;
    lily_value data;
} lily_foreign_tie;

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

/* Instances of the Dynamic class act as a wrapper around some singular value.
   Their padding exists so that their gc entry aligns with that of instances. */
typedef struct lily_dynamic_val_ {
    uint32_t refcount;
    uint32_t pad1;
    uint64_t pad2;
    struct lily_value_ *inner_value;
    struct lily_gc_entry_ *gc_entry;
} lily_dynamic_val;

/* This handles both List and Tuple. The restrictions about what can be put in
   are handled entirely at parse-time (vm just assumes correctness). There is no
   marker for this because Dynamic can't 'lose' either of those types inside of
   itself. */
typedef struct lily_list_val_ {
    uint32_t refcount;
    uint32_t extra_space;
    uint32_t num_values;
    uint32_t pad;
    struct lily_value_ **elems;
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
    uint32_t num_elems;
    uint32_t pad;
    lily_hash_elem *elem_chain;
} lily_hash_val;

/* Either an instance or an enum. The instance_id tells the id of it either way.
   This may or may not have a gc_entry set for it. */
typedef struct lily_instance_val_ {
    uint32_t refcount;
    uint16_t instance_id;
    uint16_t variant_id;
    uint32_t num_values;
    uint32_t pad;
    struct lily_value_ **values;
    struct lily_gc_entry_ *gc_entry;
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

    /* The name of the class that this function belongs to OR "". */
    const char *class_name;

    /* The name of this function, for use by debug and stack trace. */
    const char *trace_name;

    struct lily_gc_entry_ *gc_entry;

    /* The module that this function was created within. */
    struct lily_module_entry_ *module;

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
    uint64_t pad2;
    uint64_t pad3;
    struct lily_gc_entry_ *gc_entry;
} lily_generic_gc_val;



/* Next, miscellanous structs. */



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

typedef struct lily_module_link_ {
    struct lily_module_entry_ *module;
    char *as_name;
    struct lily_module_link_ *next_module;
} lily_module_link;

/* This is for when a module has a link to a library. */
typedef struct {
    /* This is the handle to the library. */
    void *source;
    /* This is the first link in the module's dynaloads. */
    const void *dynaload_table;
} lily_library;

/* A module either a single code file, or a single library that has been loaded.
   The contents inside are what the module has exported. */
typedef struct lily_module_entry_ {
    /* This links all modules within a package together, so that they can be
       iterated over and destroyed. */
    struct lily_module_entry_ *root_next;

    /* Modules have 'item_kind' set so that they can be cast to lily_item, for
       use with dynaloading. */
    uint32_t item_kind;
    /* Modules from a library are reserved a certain number of ids, so that
       they can use an 'offset' from that id to get the ids of the classes both
       inside of them and inside the interpeter. */
    uint16_t cid_start;

    uint16_t cmp_len;

    /* The name of this module. */
    char *loadname;

    /* If the path includes a directory, then this is just the directory.
       Otherwise, it's just '\0'. */
    char *dirname;

    /* The total path to this module. This may be relative to the first module,
       or an absolute path. */
    union {
        char *path;
        /* This is ONLY for the first module, which shallow-copies the path that
           is provided. Parser makes sure to NOT free this during teardown. */
        const char *const_path;
    };

    /* These links are modules that have been imported (and thus are visible)
       from this module. */
    lily_module_link *module_chain;

    /* The classes declared within this module. */
    lily_class *class_chain;

    /* The vars declared within this module. */
    lily_var *var_chain;

    /* The package that this module is contained within. */
    struct lily_package_ *parent;

    /* If the module is a shared library, then this contains a handle to that
       library. */
    lily_library *library;

    /* For modules which wrap a library (or the builtin module), then this is
       the dynaload table inside of it. */
    const void *dynaload_table;

    /* Modules that provide var seeds need to also provide a var loading
       function to load those seeds. */
    var_loader var_load_fn;
} lily_module_entry;

/* A package is a collection of modules. */
typedef struct lily_package_ {
    struct lily_package_ *root_next;

    /* The first module will probably have a standardized name, so this is the
       real name for this package. */
    char *name;

    struct lily_package_link_ *linked_packages;

    /* If this package is designated as a root package, then data inside will
       not be printed with a namespace. */
    uint64_t is_root;

    /* The first module loaded as part of this package, which contains a
       root_next to all modules loaded within this package. */
    lily_module_entry *first_module;
} lily_package;

typedef struct lily_package_link_ {
    lily_package *package;
    struct lily_package_link_ *next;
} lily_package_link;

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
#define ITEM_TYPE_MODULE   6


/* CLS_* defines are for lily_class. */


#define CLS_VALID_HASH_KEY 0x001
#define CLS_VALID_OPTARG   0x002
#define CLS_IS_ENUM        0x004
#define CLS_IS_VARIANT     0x008
/* This class is an enum AND the variants within are scoped. The difference is
   that scoped variants are accessed using 'enum.variant', while normal
   variants can use just 'variant'. */
#define CLS_ENUM_IS_SCOPED 0x010
#define CLS_EMPTY_VARIANT  0x020
/* This class can become circular, so instances must have a gc tag. */
#define CLS_GC_TAGGED      0x040
/* This class might have circular data inside of it. */
#define CLS_GC_SPECULATIVE 0x080
/* This is a temporary flag set when parser is checking of a class should have a
   gc mark/interest flag set on it. */
#define CLS_VISITED        0x100

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
#define TYPE_HAS_OPTARGS       0x08
/* This is set on a type that either is the ? type, or has a type that contains
   the ? type within it. */
#define TYPE_IS_INCOMPLETE     0x10

/* SYM_* flags are for things based off of lily_sym. */


/* properties, vars: This is used to prevent a value from being used to
   initialize itself. */
#define SYM_NOT_INITIALIZED     0x01
/* storages: This is set when the result of some expression cannot be assigned
   to. This is to prevent things like '[1,2,3][0] = 4'. */
#define SYM_NOT_ASSIGNABLE      0x02

#define SYM_CLOSED_OVER         0x04

/* properties, vars: This is 'private' to the class it was declared within. */
#define SYM_SCOPE_PRIVATE       0x08

/* properties, vars: This is 'protected' to the class it was declared within. */
#define SYM_SCOPE_PROTECTED     0x10

/* There is no 'SYM_SCOPE_PUBLIC', because public is the default. */

/* VAR_* flags are for vars. Since these have lily_sym as a superset, they begin
   where lily_sym's flags leave off. */


/* This is a var that is no longer in scope. It is kept around until the
   function it is within is done so type information can be loaded up into the
   registers later. */
#define VAR_OUT_OF_SCOPE        0x020

/* This is set on vars which will be used to hold the value of a defined
   function, a lambda, or a class constructor. Vars with this flag cannot be
   assigned to. Additionally, the reg_spot they contain is actually a spot in
   the vm's 'readonly_table'. */
#define VAR_IS_READONLY         0x040

/* This flag is set on defined functions that are found inside of other defined
   functions. Calling a function with this tag may involve the use of closures,
   so the emitter needs to wrap the given function so that it will have closure
   information. */
#define VAR_NEEDS_CLOSURE       0x100

/* Global vars need o_get_global/o_set_global opcodes to get/set them. */
#define VAR_IS_GLOBAL           0x200

/* VAL_* flags are for lily_value. */


#define VAL_IS_BOOLEAN          0x00001
#define VAL_IS_INTEGER          0x00002
#define VAL_IS_DOUBLE           0x00004
#define VAL_IS_STRING           0x00008
#define VAL_IS_BYTESTRING       0x00010
#define VAL_IS_FUNCTION         0x00020
#define VAL_IS_DYNAMIC          0x00040
#define VAL_IS_LIST             0x00080
#define VAL_IS_HASH             0x00100
#define VAL_IS_TUPLE            0x00200
#define VAL_IS_INSTANCE         0x00400
#define VAL_IS_ENUM             0x00800
#define VAL_IS_FILE             0x01000
#define VAL_IS_DEREFABLE        0x02000
#define VAL_IS_FOREIGN          0x04000
/* VAL_IS_GC_TAGGED means it is gc tagged, and must be found during a sweep. */
#define VAL_IS_GC_TAGGED        0x08000
/* VAL_IS_GC_SPECULATIVE means it might have tagged data inside. */
#define VAL_IS_GC_SPECULATIVE   0x10000
#define VAL_IS_GC_SWEEPABLE     (VAL_IS_GC_TAGGED | VAL_IS_GC_SPECULATIVE)

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
#define SYM_CLASS_EXCEPTION      16
#define SYM_CLASS_TAINTED        17
#define START_CLASS_ID           18

#endif
