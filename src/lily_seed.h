#ifndef LILY_SEED_H
# define LILY_SEED_H

typedef enum {
    dyna_class,
    dyna_function,
    dyna_var,
    dyna_exception,
    dyna_enum,
    dyna_builtin_enum,
    dyna_variant
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
    uint32_t is_refcounted;
    uint32_t generic_count;
    const void *dynaload_table;
} lily_class_seed;

typedef const struct {
    const void *next;
    char *name;
    uint64_t seed_type;
    uint32_t generic_count;
    /* dyna_builtin_enum will set this to a specific id, but dyna_enum should
       set it to 0. */
    uint32_t builtin_id;
    const void *dynaload_table;
} lily_enum_seed;

typedef const struct {
    const void *next;
    char *name;
    uint64_t seed_type;
    char *body;
    char *enum_name;
} lily_variant_seed;

#endif
