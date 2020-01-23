#ifndef LILY_CORE_TYPES_H
# define LILY_CORE_TYPES_H

# include <stdint.h>

/* This file contains structures needed by the frontend of the interpreter. */

struct lily_var_;
struct lily_type_;
struct lily_vm_state_;

typedef struct {
    struct lily_class_ *next;

    uint16_t item_kind;
    uint16_t flags;
    /* Note: The class id of a variant and of a normal class must have the same
       offset from each other. */
    uint16_t cls_id;
    uint16_t type_subtype_count;

    struct lily_type_ *build_type;

    char *name;
    uint64_t shorthash;

    uint16_t line_num;

    uint16_t pad1;

    uint32_t pad2;

    struct lily_class_ *parent;

    char *arg_names;
} lily_variant_class;

/* lily_class represents a class in the language. */
typedef struct lily_class_ {
    struct lily_class_ *next;

    uint16_t item_kind;
    uint16_t flags;
    uint16_t id;
    /* This aligns with lily_type's subtype_count. It must always be 0, so that
       the type system sees classes acting as types as being an empty type. */
    uint16_t type_subtype_count;

    /* In most cases, this is the type that you would expect if the parser were
       inside of this class and wanted to know what 'self' is.
       For classes without generics, this is actually the class itself! The
       class is cleverly laid out so that it can also be a type.
       For some built-in classes with generics, the second part holds true.
       That may not be what's expected, but it turns out to be harmless because
       built-in 'classes' (not enums) are not pattern matched against. */
    struct lily_type_ *self_type;

    char *name;
    /* This holds (up to) the first 8 bytes of the name. This is checked before
       doing a strcmp against the name. */
    uint64_t shorthash;

    uint16_t line_num;

    uint16_t pad1;

    uint32_t pad2;

    struct lily_class_ *parent;

    struct lily_named_sym_ *members;

    uint16_t inherit_depth;
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

    /* Classes that need generics will make actual types (with self_type being
       set to an appropriate 'self'). For those classes, this contains all types
       that were created within this class.
       To make it clear: Only -real- types go here. */
    struct lily_type_ *all_subtypes;
} lily_class;

typedef struct lily_type_ {
    /* All types are stored in a linked list in the symtab so they can be
       easily destroyed. */
    struct lily_type_ *next;

    uint16_t item_kind;
    uint16_t flags;
    /* If this type is for a generic, then this is the id of that generic.
       A = 0, B = 1, C = 2, etc. */
    uint16_t generic_pos;
    uint16_t subtype_count;

    lily_class *cls;

    /* If this type is -not- a class in disguise, then these are the types that
       are inside of it. Function is special cased so that [0] is the return,
       and that return may be NULL.
       If this type is actually a class, then subtype_count will be set to 0,
       and that should be checked before using this. */
    struct lily_type_ **subtypes;
} lily_type;



/* Next are the structs that emitter and symtab use to represent things. */



/* This is a superset of lily_class, as well as everything that lily_sym is a
   superset of. */
typedef struct {
    void *pad;
    uint16_t item_kind;
    uint16_t flags;
    uint16_t id;
    uint16_t pad2;
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
    uint16_t reg_spot;
    uint16_t pad2;
    lily_type *type;
} lily_sym;

typedef struct lily_named_sym_ {
    struct lily_named_sym_ *next;
    uint16_t item_kind;
    uint16_t flags;
    union {
        uint16_t reg_spot;
        uint16_t id;
    };
    uint16_t pad;
    lily_type *type;
    char *name;
    uint64_t name_shorthash;
} lily_named_sym;

/* Boxed symbols are created by direct imports `import (x, y) from z`. They're
   necessary because classes may have pending dynaloads. Since they're kept
   internal to symtab, these don't need an item kind or flags. */
typedef struct lily_boxed_sym_ {
    struct lily_boxed_sym_ *next;
    uint64_t pad;
    lily_named_sym *inner_sym;
} lily_boxed_sym;

/* This represents a property within a class that isn't "primitive" to the
   interpreter (lists, tuples, integer, string, etc.).
   User-defined classes and Exception both support these. */
typedef struct lily_prop_entry_ {
    struct lily_prop_entry_ *next;
    uint16_t item_kind;
    uint16_t flags;
    uint16_t id;
    uint16_t pad;
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
    uint16_t reg_spot;
    uint16_t pad;
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
    uint16_t reg_spot;
    uint16_t pad;
    lily_type *type;
    char *name;
    uint64_t shorthash;
    /* The line on which this var was declared. If this is a builtin var, then
       line_num will be 0. */
    uint16_t line_num;
    uint16_t pad2;
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
    struct lily_module_link_ *next;
    struct lily_module_entry_ *module;
    char *as_name;
} lily_module_link;

/* A module either a single code file, or a single library that has been loaded.
   The contents inside are what the module has exported. */
typedef struct lily_module_entry_ {
    /* All modules are linked to each other through this field. When a module is
       imported, a module link (not the raw module itself) is added into the
       source module's chain. */
    struct lily_module_entry_ *next;

    /* Modules have 'item_kind' set so that they can be cast to lily_item, for
       use with dynaloading. */
    uint16_t item_kind;

    uint16_t flags;

    uint16_t pad;

    uint16_t cmp_len;

    /* The name of this module. */
    char *loadname;

    /* If this is the first module of a package and the path includes a
       directory, this is that directory. This is NULL otherwise. When dropping
       modules, free this but not root_dirname. */
    char *dirname;

    /* The total path to this module. */
    char *path;

    /* These links are modules that have been imported (and thus are visible)
       from this module. */
    lily_module_link *module_chain;

    /* The classes declared within this module. */
    lily_class *class_chain;

    /* The vars declared within this module. */
    lily_var *var_chain;

    lily_boxed_sym *boxed_chain;

    const char *root_dirname;

    /* For modules backed by a shared library, the handle of that library. */
    void *handle;

    /* For modules which wrap a library (or the builtin module), then this is
       the dynaload table inside of it. */
    const char **info_table;

    void (**call_table)(struct lily_vm_state_ *);

    uint16_t *cid_table;
} lily_module_entry;

/* Each function holds a prototype that holds debugging information. */
typedef struct lily_proto_ {
    /* Points to the path of the module that the function resides in. */
    const char *module_path;
    /* The qualified name of the function. */
    char *name;
    /* For closures, these are indexes of locals that need to be wiped. Wiping
       these positions ensures that the cells are fresh on each invocation. */
    uint16_t *locals;
    /* This points to the code that the function is using. This makes it easier
       to free code, since there may be multiple closure function vals pointing
       at the same code. */
    uint16_t *code;
    /* If the function doesn't take keyword arguments, then this is NULL.
       Otherwise, this contains argument names for all arguments in order.
       The format is as follows:
       * If the current argument has a name, then that name is inserted, plus a
         zero terminator. Names are restricted to being whatever is considered a
         valid identifier.
       * If the current argument doesn't have a name, a space is put there
         instead.
       * The full sequence is terminated by a tab character (\t). */
    char *arg_names;
} lily_proto;


/* Finally, various definitions. */


/* These are set into the item_kind field of a symbol. These definitions are
   used to make decisions based on what kind of a symbol was found. */
#define ITEM_TYPE_VAR      1
#define ITEM_TYPE_STORAGE  2
#define ITEM_TYPE_VARIANT  3
#define ITEM_TYPE_PROPERTY 4
#define ITEM_TYPE_MODULE   5
#define ITEM_TYPE_TYPE     6
#define ITEM_TYPE_CLASS    7
#define ITEM_TYPE_ENUM     8

/* TYPE_* defines are for lily_type.
   Since classes without generics can act as their own type, class flags will
   need to start where these leave off. */


/* If set, the type is a function that takes a variable number of values. */
#define TYPE_IS_VARARGS    0x01
/* This is set on a type when it is a generic (ex: A, B, ...), or when it
   contains generics at some point. Emitter and vm use this as a fast way of
   checking if a type needs to be resolved or not. */
#define TYPE_IS_UNRESOLVED 0x02
/* This is set on function types that have at least one optional argument. This
   is set so that emitter and ts can easily figure out if the function doesn't
   have to take some arguments. */
#define TYPE_HAS_OPTARGS   0x04
/* This is not a valid type for a var or a valid solution for generics. */
#define TYPE_TO_BLOCK      0x08
/* This is set on a type that either is the ? type, or has a type that contains
   the ? type within it. */
#define TYPE_IS_INCOMPLETE 0x10
/* This is a scoop type, or has one inside somewhere. */
#define TYPE_HAS_SCOOP     0x20

/* CLS_* defines are for lily_class.
   Since classes without generics can act as their own type, these fields must
   not conflict with TYPE_* fields. */


#define CLS_VALID_HASH_KEY 0x0040
#define CLS_IS_ENUM        0x0080
/* This class can become circular, so instances must have a gc tag. */
#define CLS_GC_TAGGED      0x0100
/* This class might have circular data inside of it. */
#define CLS_GC_SPECULATIVE 0x0200
/* This class is an enum AND the variants within are scoped. The difference is
   that scoped variants are accessed using 'enum.variant', while normal
   variants can use just 'variant'. */
#define CLS_ENUM_IS_SCOPED 0x0400
#define CLS_EMPTY_VARIANT  0x0800
/* This class does not have an inheritable representation. */
#define CLS_IS_FOREIGN     0x1000
/* This is a temporary flag set when parser is checking of a class should have a
   gc mark/interest flag set on it. */
#define CLS_VISITED        0x2000

/* If either of these flag values change, then VAL_FROM_CLS_GC_SHIFT must be
   updated. */
#define CLS_GC_FLAGS       (CLS_GC_SPECULATIVE | CLS_GC_TAGGED)

/* SYM_* flags are for things based off of lily_sym. */


/* properties, vars: This is used to prevent a value from being used to
   initialize itself. */
#define SYM_NOT_INITIALIZED     0x01
/* storages: This is set when the result of some expression cannot be assigned
   to. This is to prevent things like '[1,2,3][0] = 4'. */
#define SYM_NOT_ASSIGNABLE      0x02

/* properties, vars: This is 'private' to the class it was declared within. */
#define SYM_SCOPE_PRIVATE       0x04

/* properties, vars: This is 'protected' to the class it was declared within. */
#define SYM_SCOPE_PROTECTED     0x08

/* There is no 'SYM_SCOPE_PUBLIC', because public is the default. */

/* VAR_* flags are for vars. Since these have lily_sym as a superset, they begin
   where lily_sym's flags leave off. */


/* This is set on vars which will be used to hold the value of a defined
   function, a lambda, or a class constructor. Vars with this flag cannot be
   assigned to. Additionally, the reg_spot they contain is actually a spot in
   the vm's 'readonly_table'. */
#define VAR_IS_READONLY         0x020

/* This flag is set on defined functions that are found inside of other defined
   functions. Calling a function with this tag may involve the use of closures,
   so the emitter needs to wrap the given function so that it will have closure
   information. */
#define VAR_NEEDS_CLOSURE       0x040

/* Global vars need o_get_global/o_set_global opcodes to get/set them. */
#define VAR_IS_GLOBAL           0x080

/* This var holds a function that isn't defined in Lily. This is used by the
   emitter to write specialized code when the target is known to be a foreign
   function. */
#define VAR_IS_FOREIGN_FUNC     0x100

/* Static functions don't receive an implicit 'self'. */
#define VAR_IS_STATIC           0x200

/* This is an incomplete forward declaration. */
#define VAR_IS_FORWARD          0x400

/* Don't put this var inside of a closure. This is set on class constructor
   parameters to prevent class methods from referencing them. Preventing this
   makes implementing closures much easier. */
#define VAR_CANNOT_BE_UPVALUE   0x800

/* This module was added by being registered. */
#define MODULE_IS_REGISTERED 0x1

/* This is a new module that hasn't been fully executed yet. */
#define MODULE_NOT_EXECUTED  0x2

/* This module is currently being executed. */
#define MODULE_IN_EXECUTION  0x4

/* This is a module that is defined in Lily's core like math or sys. This module
   is available everywhere. */
#define MODULE_IS_PREDEFINED 0x8

/* Storages that are locked will not be overwritten by another value. */
#define STORAGE_IS_LOCKED 0x1

#define LILY_LAST_ID       65528
/* Instances of these are never made, so these ids will never be seen by vm. */
#define LILY_ID_SELF       65529
#define LILY_ID_QUESTION   65530
#define LILY_ID_GENERIC    65531
#define LILY_ID_OPTARG     65532
#define LILY_ID_SCOOP      65534
#endif
