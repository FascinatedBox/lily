#ifndef LILY_VALUE_STRUCTS_H
# define LILY_VALUE_STRUCTS_H

# include <stdint.h>
# include <stdio.h>

struct lily_vm_state_;
struct lily_value_;

/* Lily's foreign functions look like this. */
typedef void (*lily_foreign_func)(struct lily_vm_state_ *);
/* This function is called when a value tagged as refcounted drops to 0 refs.
   This handles a value of a given class (regardless of type) and frees what is
   inside. */
typedef void (*class_destroy_func)(struct lily_value_ *);

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

/* This is the var of a value that's being stored in the parser until the vm is
   ready to receive it. */
typedef struct lily_foreign_value_ {
    uint32_t flags;
    uint16_t pad;
    uint16_t reg_spot;
    lily_raw_value value;
} lily_foreign_value;

/* Literals are stored in an array, and for simplicity, each one of a particular
   group holds where the next in. So, for example, the Integers all know where
   the next one is with the last holding a next of 0. */
typedef struct lily_literal_ {
    uint32_t flags;
    uint16_t reg_spot;
    uint16_t next_index;
    lily_raw_value value;
} lily_literal;

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

/* A proper Lily value. The 'flags' field holds gc/deref info, as well as a
   VAL_IS_* flag to indicate the -kind- of value. */
typedef struct lily_value_ {
    uint32_t flags;
    /* This is only used by closure cells. When a closure cell has a
       cell_refcount of zero, it's deref'd and free'd. */
    uint32_t cell_refcount;
    lily_raw_value value;
} lily_value;

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

    /* Here's where the function's code is stored. */
    uint16_t *code;

    uint32_t pad;

    uint16_t num_upvalues;

    /* This is how many registers that this function uses. */
    uint16_t reg_count;

    union {
        struct lily_value_ **upvalues;
        /* A function's cid table holds a mapping that's used to obtain class
           ids for dynaloaded classes. */
        uint16_t *cid_table;
    };
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

#endif
