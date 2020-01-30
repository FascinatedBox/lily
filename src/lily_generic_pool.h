#ifndef LILY_GENERIC_POOL_H
# define LILY_GENERIC_POOL_H

/* The generic pool is used to store generic classes. It handles management of
   what generics are available, and of creating them if need be.
   There's no index for 'cache_generics'. Instead, it will always have one class
   set aside as NULL as a sentinel. It's not ideal, but it prevents having to
   use another uint64_t. */
typedef struct {
    struct lily_class_ **cache_generics;
    struct lily_class_ **scope_generics;
    uint16_t cache_size;

    uint16_t scope_start;
    uint16_t scope_end;
    uint16_t scope_size;
} lily_generic_pool;

lily_generic_pool *lily_new_generic_pool(void);

/* Try to find a generic of the given name in the pool. */
struct lily_class_ *lily_gp_find(lily_generic_pool *, const char *);

/* Add a generic to the current scope. If a generic that matches the name and
   the generic position is given, then that generic is used. Otherwise, a new
   generic is created. The generic is returned. */
lily_type *lily_gp_push(lily_generic_pool *, const char *, int);

/* How many generics are in the current scope? */
int lily_gp_num_in_scope(lily_generic_pool *);

/* Save the end of the current scope, to restore later. The generics from the
   old scope are still visible. */
uint16_t lily_gp_save(lily_generic_pool *);

/* Save the start of the current scope, to unhide later. You should only use
   this if there cannot be a prior scope (such as how classes and enums do not
   allow nesting OR when running a dynaload). */
uint16_t lily_gp_save_and_hide(lily_generic_pool *);

/* Restore the end of the current scope. */
void lily_gp_restore(lily_generic_pool *, uint16_t);

/* Unhide generics from the old scope. */
void lily_gp_restore_and_unhide(lily_generic_pool *, uint16_t);

void lily_free_generic_pool(lily_generic_pool *);

#endif
