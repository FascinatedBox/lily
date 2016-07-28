#include "lily_core_types.h"
#include "lily_generic_pool.h"
#include "lily_symtab.h"
#include "lily_type_maker.h"

#include "lily_api_alloc.h"

lily_generic_pool *lily_new_generic_pool(void)
{
    lily_generic_pool *gp = lily_malloc(sizeof(lily_generic_pool));
    lily_class **cache_generics = lily_malloc(4 * sizeof(lily_class *));
    lily_class **scope_generics = lily_malloc(4 * sizeof(lily_class *));

    gp->cache_generics = cache_generics;
    gp->cache_size = 4;

    int i;
    for (i = 0;i < 4;i++)
        cache_generics[i] = NULL;

    gp->scope_generics = scope_generics;
    gp->scope_start = 0;
    gp->scope_end = 0;
    gp->scope_size = 4;

    return gp;
}

void lily_free_generic_pool(lily_generic_pool *gp)
{
    int i;
    for (i = 0;i < gp->cache_size;i++) {
        lily_class *c = gp->cache_generics[i];
        if (c == NULL)
            break;

        lily_free(c->type);
        lily_free(c->name);
        lily_free(c);
    }

    lily_free(gp->cache_generics);
    lily_free(gp->scope_generics);
    lily_free(gp);
}

static lily_class *find_in_cache(lily_generic_pool *gp, const char *name,
        int generic_pos, int *next_pos)
{
    int i = 0;
    lily_class *c = gp->cache_generics[i];

    while (c) {
        if (c->name[0] == name[0])
            return c;

        i++;
        c = gp->cache_generics[i];
    }

    *next_pos = i;
    return NULL;
}

void lily_gp_push(lily_generic_pool *gp, const char *name, int generic_pos)
{
    int i;
    lily_class *result = find_in_cache(gp, name, generic_pos, &i);

    if (result == NULL) {
        lily_class *new_generic = lily_new_raw_class(name);
        lily_type *t = lily_new_raw_type(new_generic);

        t->flags |= TYPE_IS_UNRESOLVED;
        t->generic_pos = generic_pos;

        new_generic->id = SYM_CLASS_GENERIC;
        new_generic->type = t;
        new_generic->all_subtypes = t;

        result = new_generic;
        gp->cache_generics[i] = new_generic;

        if (i + 1 == gp->cache_size) {
            gp->cache_size *= 2;
            lily_class **new_cache = lily_realloc(gp->cache_generics,
                    gp->cache_size * sizeof(lily_class *));

            for (i = i + 1;i < gp->cache_size;i++)
                new_cache[i] = NULL;

            gp->cache_generics = new_cache;
        }
    }

    if (gp->scope_end == gp->scope_size) {
        gp->scope_size *= 2;
        lily_class **new_scope = lily_realloc(gp->scope_generics,
                gp->scope_size * sizeof(lily_class *));

        gp->scope_generics = new_scope;
    }

    gp->scope_generics[gp->scope_end] = result;
    gp->scope_end++;
}

lily_class *lily_gp_find(lily_generic_pool *gp, const char *name)
{
    char ch = name[0];
    int i;

    for (i = gp->scope_start;i < gp->scope_end;i++) {
        lily_class *c = gp->scope_generics[i];
        if (c->name[0] == ch)
            return c;
    }

    return NULL;
}

int lily_gp_num_in_scope(lily_generic_pool *gp)
{
    return gp->scope_end - gp->scope_start;
}

void lily_gp_save(lily_generic_pool *gp, int *save_end)
{
    *save_end = gp->scope_end;
}

void lily_gp_restore(lily_generic_pool *gp, int old_end)
{
    gp->scope_end = old_end;
}

void lily_gp_save_and_hide(lily_generic_pool *gp, int *save_start)
{
    *save_start = gp->scope_start;
    gp->scope_start = gp->scope_end;
}

void lily_gp_restore_and_unhide(lily_generic_pool *gp, int old_start)
{
    gp->scope_end = gp->scope_start;
    gp->scope_start = old_start;
}
