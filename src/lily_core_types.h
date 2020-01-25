#ifndef LILY_CORE_TYPES_H
# define LILY_CORE_TYPES_H

# include <stdint.h>

/* This file contains structs used by the interpreter's frontend to represent
   symbols, plus important flags and class ids. These structs have been aligned
   to make symbol handling easier and to allow for optimizations.
   One of those optimizations relates to classes/enums and types. Since those
   structs contain the same layout, a class/enum that does not use generics
   (term: monomorphic) can act as its own type. */

struct lily_vm_state_;

/* This is a subset of every struct with an item_kind field. The item_kind field
   can be used to determine what struct to cast this to. */
typedef struct {
    void *pad1;

    uint16_t item_kind;
    uint16_t flags;
    uint32_t pad2;
} lily_item;

/* This is a subset of lily_prop_entry, lily_storage, and lily_var. */
typedef struct lily_sym_ {
    void *pad;

    uint16_t item_kind;
    uint16_t flags;
    /* This is only a valid spot for local vars and storages. */
    uint16_t reg_spot;
    uint16_t pad2;

    struct lily_type_ *type;
} lily_sym;

/* This is a subset of lily_prop_entry and lily_var. This is used in certain
   cases where a symbol is known to be a class method or class property. */
typedef struct lily_named_sym_ {
    struct lily_named_sym_ *next;

    uint16_t item_kind;
    uint16_t flags;
    union {
        uint16_t reg_spot;
        uint16_t id;
    };
    uint16_t pad;

    struct lily_type_ *type;

    char *name;

    uint64_t shorthash;
} lily_named_sym;

/* This represents a type for a property, storage, or var. Instances of this are
   only created by type maker after verifying uniqueness. */
typedef struct lily_type_ {
    /* Every instance of a type for a particular class/enum is linked together
       and stored in that class/enum.
       This should not be traversed if this is a class in disguise. */
    struct lily_type_ *next;

    /* If this really is a type, this is ITEM_TYPE_TYPE. */
    uint16_t item_kind;
    /* See TYPE_* flags. */
    uint16_t flags;
    /* If this type is a generic, this is the id (A = 0, B = 1, ...). */
    uint16_t generic_pos;
    /* A count of how many types are inside of this one. If this is a class in
       disguise, this is 0 and the subtype field is invalid. Otherwise, this is
       at least 1 and the last type is count - 1. */
    uint16_t subtype_count;

    /* This is always the class this type corresponds to. */
    struct lily_class_ *cls;

    /* If this is an instance of a type, these are those types (and they're
       never NULL). If this is a class in disguise, this is invalid data.
       For `Function`, the return type is first at 0. */
    struct lily_type_ **subtypes;
} lily_type;

/* This represents a class or enum. */
typedef struct lily_class_ {
    struct lily_class_ *next;

    /* This is ITEM_TYPE_CLASS or ITEM_TYPE_ENUM. */
    uint16_t item_kind;
    /* See CLS_* flags. */
    uint16_t flags;
    uint16_t id;
    /* For monomorphic classes/enums, this is always 0. */
    uint16_t type_subtype_count;

    /* This is the type that `self` would have if parser was inside of this
       class/enum. If this is a monomorphic class/enum, this is the class/enum
       but cast as lily_type.
       The only exceptions are the magic classes `Function` and `Tuple`, and
       predefined polymorphic classes which leave this as NULL since they don't
       use it. */
    struct lily_type_ *self_type;

    char *name;

    uint64_t shorthash;

    uint16_t line_num;
    uint16_t pad1;
    uint32_t pad2;

    struct lily_class_ *parent;

    struct lily_named_sym_ *members;

    /* A count of how many parent classes this class has. If this has none or
       this represents an enum, this is 0. */
    uint16_t inherit_depth;
    /* When declaring a type of this class, how many types need to be placed in
       brackets (`Integer` = 0, `List` = 1, `Hash` = 2).
       For magic types (`Function` and `Tuple`), this is -1. */
    int16_t generic_count;
    union {
        /* Native classes: How many properties this class needs in total. */
        uint16_t prop_count;
        /* Enums: How many variants are in this enum. */
        uint16_t variant_size;
    };
    /* For classes/enums that come from a foreign module, this is where their
       data begins in the module's dynaload table (and it's never 0).
       For classes/enums from a native module, this is always 0. */
    uint16_t dyna_start;

    /* This is the module that the class/enum comes from, used by dynaload. */
    struct lily_module_entry_ *module;

    /* For polymorphic classes/enums, this contains a linked list of all types
       that represent this class. */
    struct lily_type_ *all_subtypes;
} lily_class;

/* This represents a variant inside of an enum. This is aligned to lily_class so
   that it can be used in the vm's class table. */
typedef struct {
    lily_named_sym *next;

    /* This is always ITEM_TYPE_VARIANT. */
    uint16_t item_kind;
    /* See CLS_* flags. */
    uint16_t flags;
    uint16_t cls_id;
    uint16_t type_subtype_count;

    /* If this is an empty variant, this is the variant cast to a type. The type
       must not be inserted into the type system.
       If this is a variant that takes arguments, this is a `Function` that
       takes those arguments and returns the enum's self type. */
    lily_type *build_type;

    char *name;

    uint64_t shorthash;

    uint16_t line_num;
    uint16_t pad1;
    uint32_t pad2;

    /* A variant's parent is the enum it belongs to. */
    struct lily_class_ *parent;

    /* See lily_proto's arg_names. */
    char *arg_names;
} lily_variant_class;

/* This represents a property inside of a class. */
typedef struct {
    lily_named_sym *next;

    /* This is always ITEM_TYPE_PROPERTY. */
    uint16_t item_kind;
    /* See SYM_* flags. */
    uint16_t flags;
    uint16_t id;
    uint16_t pad;

    struct lily_type_ *type;

    char *name;

    uint64_t shorthash;

    lily_class *parent;
} lily_prop_entry;

/* This represents class/enum methods, defined functions, lambdas, and vars. */
typedef struct lily_var_ {
    struct lily_var_ *next;

    /* This is always ITEM_TYPE_VAR. */
    uint16_t item_kind;
    /* See VAR_* flags. */
    uint16_t flags;
    /* Every class method, defined function, and lambda occupies a spot in the
       vm's readonly table. For those, this is their position in that table.
       For global vars, this is the var's global index.
       For non-global vars, this is the spot this var occupies in the function
       it was declared in. */
    uint16_t reg_spot;
    uint16_t pad;

    lily_type *type;

    char *name;

    uint64_t shorthash;

    uint16_t line_num;
    uint16_t pad2;
    /* This is used to determine if a var is an upvalue, local, or global. */
    uint32_t function_depth;

    /* If this is a class/enum method, this is the parent. NULL otherwise. */
    lily_class *parent;
} lily_var;

/* This represents an import of a whole module. These don't escape symtab, so
   they don't need to align to lily_sym. */
typedef struct lily_module_link_ {
    struct lily_module_link_ *next;

    /* This is the module being imported. */
    struct lily_module_entry_ *module;

    /* If the module was imported with a certain name (ex: `import a as b`),
       then this is that name. NULL otherwise. */
    char *as_name;
} lily_module_link;

/* This represents an import of a symbol (ex: `import (a, b, c) def`). These
   don't escape symtab, so they don't need to align to lily_sym. */
typedef struct lily_boxed_sym_ {
    struct lily_boxed_sym_ *next;
    uint64_t pad;
    lily_named_sym *inner_sym;
} lily_boxed_sym;

/* This represents a single file or library. */
typedef struct lily_module_entry_ {
    /* All modules loaded are linked together through here. */
    struct lily_module_entry_ *next;

    /* This is always ITEM_TYPE_MODULE. */
    uint16_t item_kind;
    /* See MODULE_* flags. */
    uint16_t flags;
    uint16_t pad;
    /* This is set to the length of the path. When searching for modules by path
       to find a duplicate, this is checked before the path.
       Modules can be hidden from path search by setting this to 0. */
    uint16_t cmp_len;

    /* This is the default name this module will appear with when imported by
       other modules. Usually 'basename(path) - suffix'. */
    char *loadname;

    /* If this is the first module of a package (and the path has a directory),
       this is the directory. NULL otherwise.
       When getting rid of modules, free this but not root_dirname. */
    char *dirname;

    /* This is the path to this module. For predefined modules, this is the
       loadname in brackets (ex: '[sys]'). */
    char *path;

    /* These are modules that this module has imported. */
    lily_module_link *module_chain;

    /* These are classes/enums declared in this module. */
    lily_class *class_chain;

    /* These are the vars and defined functions declared in this module. */
    lily_var *var_chain;

    /* These are symbols directly imported (`import (a, b, c) def`). */
    lily_boxed_sym *boxed_chain;

    /* This points to the dirname of the first module in the package that this
       module belongs to. */
    const char *root_dirname;

    /* If this is a foreign module (wraps over a library), this is the
       underlying handle to it. NULL otherwise. */
    void *handle;

    /* If this is a foreign or predefined module, this is the info table used
       for loading content. NULL otherwise. */
    const char **info_table;

    /* Same as above. */
    void (**call_table)(struct lily_vm_state_ *);

    /* This table allows foreign functions to use classes, enums, and variants
       that don't have hardcoded ids. If this modules does not need that, then
       this is NULL. */
    uint16_t *cid_table;
} lily_module_entry;

/* Each function value has one of these to hold debug information. */
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


/* ITEM_TYPE_* flags are for item_kind. */


#define ITEM_TYPE_CLASS    1
#define ITEM_TYPE_ENUM     2
#define ITEM_TYPE_MODULE   3
#define ITEM_TYPE_PROPERTY 4
#define ITEM_TYPE_STORAGE  5
#define ITEM_TYPE_TYPE     6
#define ITEM_TYPE_VAR      7
#define ITEM_TYPE_VARIANT  8


/* TYPE_* flags are for lily_type. */


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


/* CLS_* flags are for lily_class and lily_variant_class. Since both of those
   can be types, these start after TYPE_* flags. */


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


/* SYM_* flags apply to lily_prop_entry, lily_var, and lily_storage. */


/* This is set on vars by parser to prevent them from self initializing. */
#define SYM_NOT_INITIALIZED 0x01

/* This is set on storages by emitter to prevent assignment. This blocks code
   such as `[1, 2, 3][0] = 4` or `somefunc() = 2`. */
#define SYM_NOT_ASSIGNABLE  0x02

/* This var or property is private. If this and the protected flag are both
   absent, the var or property is public. */
#define SYM_SCOPE_PRIVATE   0x04

/* This var or property is protected. */
#define SYM_SCOPE_PROTECTED 0x08


/* STORAGE_* flags are for lily_storage. */


/* Storages that are locked will not be overwritten by another value. */
#define STORAGE_IS_LOCKED 0x100


/* VAR_* flags only apply to lily_var and start after SYM_* flags. */


/* This is set on vars which will be used to hold the value of a defined
   function, a lambda, or a class constructor. Vars with this flag cannot be
   assigned to. Additionally, the reg_spot they contain is actually a spot in
   the vm's 'readonly_table'. */
#define VAR_IS_READONLY       0x020

/* This flag is set on defined functions that are found inside of other defined
   functions. Calling a function with this tag may involve the use of closures,
   so the emitter needs to wrap the given function so that it will have closure
   information. */
#define VAR_NEEDS_CLOSURE     0x040

/* Global vars need o_get_global/o_set_global opcodes to get/set them. */
#define VAR_IS_GLOBAL         0x080

/* This var holds a function that isn't defined in Lily. This is used by the
   emitter to write specialized code when the target is known to be a foreign
   function. */
#define VAR_IS_FOREIGN_FUNC   0x100

/* Static functions don't receive an implicit 'self'. */
#define VAR_IS_STATIC         0x200

/* This is an incomplete forward declaration. */
#define VAR_IS_FORWARD        0x400

/* Don't put this var inside of a closure. This is set on class constructor
   parameters to prevent class methods from referencing them. Preventing this
   makes implementing closures much easier. */
#define VAR_CANNOT_BE_UPVALUE 0x800


/* MODULE_* flags only apply to lily_module_entry. */


/* This module was added by being registered. */
#define MODULE_IS_REGISTERED 0x1

/* This is a new module that hasn't been fully executed yet. */
#define MODULE_NOT_EXECUTED  0x2

/* This module is currently being executed. */
#define MODULE_IN_EXECUTION  0x4

/* This is a module that is defined in Lily's core like math or sys. This module
   is available everywhere. */
#define MODULE_IS_PREDEFINED 0x8


/* These are important ids in the interpreter. */


#define LILY_LAST_ID       65528
/* Instances of these are never made, so these ids will never be seen by vm. */
#define LILY_ID_SELF       65529
#define LILY_ID_QUESTION   65530
#define LILY_ID_GENERIC    65531
#define LILY_ID_OPTARG     65532
#define LILY_ID_SCOOP      65534

#endif
