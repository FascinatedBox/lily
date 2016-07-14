#ifndef LILY_CORE_TYPES_H
# define LILY_CORE_TYPES_H

# include <stdint.h>

/* This file contains structures needed by the frontend of the interpreter. */

struct lily_var_;
struct lily_type_;
struct lily_options_;

/* A module that has a dynaload table should also come with a loader. The loader
   is responsible for fetching functions and initializing variables. */
typedef void *(*lily_loader)(struct lily_options_ *, uint16_t *, int);

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
        struct lily_literal_ *default_value;
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

    /* If a class does not take generics, then this is set and it is a single
       type that is shared by all. NULL otherwise. */
    struct lily_type_ *type;

    struct lily_class_ *parent;

    struct lily_named_sym_ *members;

    /* If it's an enum, then the variants are here. NULL otherwise. */
    lily_variant_class **variant_members;

    uint16_t id;
    /* If positive, how many subtypes are allowed in this type. This can also
       be -1 if an infinite number of types are allowed (ex: functions). */
    int16_t generic_count;
    union {
        uint16_t prop_count;
        uint16_t variant_size;
    };
    uint16_t dyna_start;

    /* This is the module that this class was defined within. This is sometimes
       used for establishing a scope when doing dynaloading. */
    struct lily_module_entry_ *module;

    /* Every type that has this as its class can be found here. The type maker
       (which is responsible for creating new types) will always build the
       'self' type of any class first, as well as ensuring that it is always
       first. */
    struct lily_type_ *all_subtypes;
} lily_class;

typedef struct lily_type_ {
    lily_class *cls;

    uint16_t item_kind;
    uint16_t flags;
    /* If this type is for a generic, then this is the id of that generic.
       A = 0, B = 1, C = 2, etc. */
    uint16_t generic_pos;
    uint16_t subtype_count;

    /* If this type has subtypes (ex: A list has a subtype that explains what
       type is allowed inside), then this is where those subtypes are.
       Functions are a special case, where subtypes[0] is either their return
       type, or NULL. */
    struct lily_type_ **subtypes;

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

typedef struct lily_named_sym_ {
    struct lily_named_sym_ *next;
    uint16_t item_kind;
    uint16_t flags;
    uint32_t reg_spot;
    lily_type *type;
    char *name;
    uint64_t name_shorthash;
} lily_named_sym;

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
    char *name;
    uint64_t shorthash;
    /* The line on which this var was declared. If this is a builtin var, then
       line_num will be 0. */
    uint32_t line_num;
    /* How deep that functions were when this var was declared. If this is 1,
       then the var is in __main__ and a global. Otherwise, it is a local.
       This is an important difference, because the vm has to do different
       loads for globals versus locals. */
    uint32_t function_depth;
    /* (Up to) the first 8 bytes of the name. This is compared before comparing
       the name. */
    struct lily_class_ *parent;
} lily_var;


/* Next, miscellanous structs. */



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
    const char **dynaload_table;
} lily_library;

/* A module either a single code file, or a single library that has been loaded.
   The contents inside are what the module has exported. */
typedef struct lily_module_entry_ {
    /* This links all modules within a package together, so that they can be
       iterated over and destroyed. */
    struct lily_module_entry_ *root_next;

    /* Modules have 'item_kind' set so that they can be cast to lily_item, for
       use with dynaloading. */
    uint16_t item_kind;

    uint16_t flags;

    uint16_t pad;

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

    /* For modules backed by a shared library, the handle of that library. */
    void *handle;

    /* For modules which wrap a library (or the builtin module), then this is
       the dynaload table inside of it. */
    const char **dynaload_table;

    lily_loader loader;

    uint16_t *cid_table;
} lily_module_entry;

/* A package is a collection of modules. */
typedef struct lily_package_ {
    struct lily_package_ *root_next;

    /* The first module will probably have a standardized name, so this is the
       real name for this package. */
    char *name;

    struct lily_package_link_ *linked_packages;

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
#define ITEM_TYPE_VAR      1
#define ITEM_TYPE_STORAGE  2
#define ITEM_TYPE_VARIANT  3
#define ITEM_TYPE_PROPERTY 4
#define ITEM_TYPE_MODULE   5
#define ITEM_TYPE_TYPE     6
#define ITEM_TYPE_CLASS    7

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
/* This class does not have an inheritable representation. */
#define CLS_IS_BUILTIN     0x100
/* This is a temporary flag set when parser is checking of a class should have a
   gc mark/interest flag set on it. */
#define CLS_VISITED        0x200

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
/* This is a scoop type, or has one inside somewhere. */
#define TYPE_HAS_SCOOP         0x20

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

/* This var holds a function that isn't defined in Lily. This is used by the
   emitter to write specialized code when the target is known to be a foreign
   function. */
#define VAR_IS_FOREIGN_FUNC     0x400

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
#define SYM_CLASS_FILE           10
#define SYM_CLASS_GENERIC        11
#define SYM_CLASS_QUESTION       12
#define SYM_CLASS_OPTION         13
#define SYM_CLASS_EITHER         14
#define SYM_CLASS_EXCEPTION      15
/* This order must be synced with the dynaload order of these classes. */
#define SYM_CLASS_IOERROR        16
#define SYM_CLASS_FORMATERROR    17
#define SYM_CLASS_KEYERROR       18
#define SYM_CLASS_RUNTIMEERROR   19
#define SYM_CLASS_VALUEERROR     20
#define SYM_CLASS_INDEXERROR     21
#define SYM_CLASS_DBZERROR       22 /* > 9000 */
#define SYM_CLASS_TAINTED        23
#define START_CLASS_ID           24

/* Instances of these are never made, so these ids will never be seen by vm. */
#define SYM_CLASS_OPTARG      65532
#define SYM_CLASS_SCOOP_2     65533
#define SYM_CLASS_SCOOP_1     65534
#define LOWEST_SCOOP_ID       65533
#endif
