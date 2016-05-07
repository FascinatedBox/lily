#ifndef LILY_API_DYNALOAD_H
# define LILY_API_DYNALOAD_H

typedef enum {
    dyna_class,
    dyna_function,
    dyna_var,
    dyna_bootstrap_class,
    dyna_enum,
    dyna_builtin_enum,
    dyna_variant
} dyna_type;

typedef struct lily_base_seed_ {
    const void *next;
    char *name;
    uint64_t seed_type;
} lily_base_seed;

typedef struct {
    const void *next;
    char *name;
    uint64_t seed_type;
    char *type;
} lily_var_seed;

typedef struct {
    const void *next;
    char *name;
    uint64_t seed_type;
    char *func_definition;
    lily_foreign_func func;
} lily_func_seed;

typedef struct {
    const void *next;
    char *name;
    uint64_t seed_type;
    uint32_t is_refcounted;
    uint32_t generic_count;
    const void *dynaload_table;
} lily_class_seed;

typedef struct {
    const void *next;
    char *name;
    uint64_t seed_type;
    uint64_t class_id;
    char *parent;
    char *body;
} lily_bootstrap_seed;

typedef struct {
    const void *next;
    char *name;
    uint64_t seed_type;
    uint32_t generic_count;
    uint32_t builtin_id;
    const void *dynaload_table;
} lily_enum_seed;

typedef struct {
    const void *next;
    char *name;
    uint64_t seed_type;
    char *body;
    char *enum_name;
} lily_variant_seed;

#define DYNA_FUNCTION_RAW(scope, dyna, prev, cur, name, definition) \
scope const lily_func_seed cur = \
    {prev, #name, dyna_function, definition, lily_##dyna##_##name};

#define DYNA_FUNCTION_MIDDLE(mods, dyna, prev, name, definition) \
DYNA_FUNCTION_RAW(mods, dyna, prev, seed_##name, name, definition)

#define DYNA_FUNCTION(prev, name, definition) \
DYNA_FUNCTION_MIDDLE(static, DYNA_NAME, prev, name, definition)

#endif
