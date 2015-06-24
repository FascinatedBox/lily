#ifndef LILY_SEED_H
# define LILY_SEED_H

typedef enum {
    dyna_class,
    dyna_function,
    dyna_var,
    dyna_exception
} dyna_type;

typedef const struct lily_base_seed_ {
    const void *next;
    char *name;
    uint64_t seed_type;
} lily_base_seed;

typedef const struct {
    const void *next;
    char *name;
    uint64_t seed_type;
    char *type;
} lily_var_seed;

typedef const struct {
    const void *next;
    char *name;
    uint64_t seed_type;
    char *func_definition;
    lily_foreign_func func;
} lily_func_seed;

typedef const struct {
    const void *next;
    char *name;
    uint64_t seed_type;
    uint16_t is_refcounted;
    uint16_t generic_count;
    uint32_t flags;
    const void *dynaload_table;
    gc_marker_func gc_marker;
    class_eq_func eq_func;
    class_destroy_func destroy_func;
} lily_class_seed;

#endif
